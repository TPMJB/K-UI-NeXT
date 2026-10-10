// K-UI NeXT G1 front end. Original implementation; no DreamShell FPGA code.
// Conservative 50 MHz sampled ATA PIO and MCU asynchronous byte link.
// Physical timing/fit with Lattice Diamond is a release requirement.
`default_nettype none
module bridge #(
    parameter FIFO_AW = 9,
    parameter READ_HOLD_CYCLES = 2,
    parameter BIOS_INITIAL_BANK = 2'b00
)(
    input wire FPGA_CLK50,
    inout wire [7:0] LINK_D,
    input wire LINK_WRn, LINK_RDn,
    input wire LINK_A0, LINK_A1,
    output wire LINK_IRQ, LINK_READY,
    input wire FPGA_RESETn, BRIDGE_ARM, BUS_SAFE, PWR_FAILn,
    inout wire [15:0] BUS_DD,
    input wire [2:0] BUS_DA,
    input wire BUS_CS0n, BUS_CS1n, BUS_DIORn, BUS_DIOWn,
    input wire BUS_RESETn, BUS_DMACKn,
    output wire REQ_DATA_OEn, DATA_DIR,
    output wire REQ_IORDY_LOW, REQ_INTRQ, REQ_DMARQ,
    output wire REQ_INTRQ_OEn, REQ_DMARQ_OEn,
    output wire FPGA_READY,
    output reg BIOS_BANK0, BIOS_BANK1,
    output wire BIOS_WR_EN,
    input wire BIOS_RECOVERYn
);
    localparam DEPTH = 1 << FIFO_AW;
    // BUS_SAFE is deliberately absent from FPGA_READY: hardware gates BUS_SAFE
    // with READY, so using it here would create a startup dependency loop.
    reg [2:0] start_pipe;
    wire link_resetn = FPGA_RESETn & PWR_FAILn;
    always @(posedge FPGA_CLK50 or negedge link_resetn)
        if (!link_resetn) start_pipe <= 0;
        else start_pipe <= {start_pipe[1:0],1'b1};
    assign FPGA_READY = start_pipe[2] & link_resetn;
    wire drive_safe = BUS_SAFE & BRIDGE_ARM & FPGA_READY & BUS_RESETn & BIOS_RECOVERYn;

    (* ASYNC_REG="TRUE" *) reg [2:0] wr_pipe, rd_pipe, dior_pipe, diow_pipe;
    reg [7:0] link_write_latch;
    reg [1:0] link_addr_latch, link_read_addr_latch;
    reg [15:0] ata_write_latch;
    reg [2:0] ata_addr_latch, ata_read_addr;
    reg ata_cs0_latch, ata_cs1_latch, ata_dma_latch;
    always @(posedge FPGA_CLK50 or negedge link_resetn) begin
        if (!link_resetn) begin
            wr_pipe<=7; rd_pipe<=7; dior_pipe<=7; diow_pipe<=7;
            link_write_latch<=0; link_addr_latch<=0; link_read_addr_latch<=0;
            ata_write_latch<=0; ata_addr_latch<=0; ata_read_addr<=0;
            ata_cs0_latch<=0; ata_cs1_latch<=0; ata_dma_latch<=0;
        end else begin
            wr_pipe<={wr_pipe[1:0],LINK_WRn};
            rd_pipe<={rd_pipe[1:0],LINK_RDn};
            dior_pipe<={dior_pipe[1:0],BUS_DIORn};
            diow_pipe<={diow_pipe[1:0],BUS_DIOWn};
            // Sample while write strobe is LOW. Do not sample live data several
            // synchronizer cycles after its rising edge (ATA hold may be short).
            if (!LINK_WRn) begin link_write_latch<=LINK_D; link_addr_latch<={LINK_A1,LINK_A0}; end
            if (!LINK_RDn) link_read_addr_latch<={LINK_A1,LINK_A0};
            if (!BUS_DIOWn) begin
                ata_write_latch<=BUS_DD; ata_addr_latch<=BUS_DA;
                ata_cs0_latch<=!BUS_CS0n; ata_cs1_latch<=!BUS_CS1n;
                ata_dma_latch<=!BUS_DMACKn;
            end
            if (!BUS_DIORn) ata_read_addr<=BUS_DA;
        end
    end
    wire link_wr = !wr_pipe[2] & wr_pipe[1];
    wire link_rd_end = !rd_pipe[2] & rd_pipe[1];
    wire ata_wr_end = !diow_pipe[2] & diow_pipe[1];
    wire ata_rd_begin = dior_pipe[2] & !dior_pipe[1];
    wire ata_rd_end = !dior_pipe[2] & dior_pipe[1];
    wire [1:0] link_addr = {LINK_A1,LINK_A0};
    reg [7:0] index_reg;
    reg tx_byte_high, rx_byte_high;
    reg [7:0] tx_low;
    reg [7:0] feature, sector_count, lba0, lba1, lba2, dev_head;
    reg [7:0] command, status_reg, error_reg, control_reg;
    reg command_pending, intrq_pending, fault, transfer_done;
    reg diagnostic_pending;
    reg [23:0] transfer_words;
    reg [1:0] transfer_mode; // 0 PIO; 1 DMA read; 2 DMA write; 3 forbidden
    reg bios_write_request;
    reg [15:0] read_latch;
    reg read_owned;
    reg [7:0] hold_count;
    wire selected = dev_head[4];
    wire soft_reset = control_reg[2];
    reg bridge_active, unlock_pending, safety_event_pending;
    reg [15:0] unlock_age;
    reg [4:0] fresh_fields;
    // Activation is independent of the hardware ARM gate. Even a sub-clock
    // safety pulse invalidates activation and any partially supplied key.
    wire activation_resetn = link_resetn & BRIDGE_ARM & BUS_SAFE &
        BUS_RESETn & BIOS_RECOVERYn & !soft_reset;
    wire taskfile_write = ata_wr_end & drive_safe & !soft_reset &
        !status_reg[7] & (!selected | !status_reg[3]) &
        !ata_dma_latch & ata_cs0_latch;
    wire command_write = taskfile_write & (ata_addr_latch==7);
    wire vendor_frame = command_write & selected & (dev_head==8'hb0) &
        (&fresh_fields) & (ata_write_latch[7:0]==8'hf0);
    wire unlock_first = vendor_frame & (feature==8'h4b) &
        (sector_count==8'h55) & (lba0==8'h49) & (lba1==8'h01) & (lba2==8'ha5);
    wire unlock_second = vendor_frame & (feature==8'hb4) &
        (sector_count==8'haa) & (lba0==8'hb6) & (lba1==8'hfe) & (lba2==8'h5a);
    wire relock_frame = vendor_frame & (feature==8'h4c) &
        (sector_count==8'h4f) & (lba0==8'h43) & (lba1==8'h4b) & (lba2==0);
    wire mcu_relock = link_wr & (link_addr_latch==3) &
        (index_reg==8'h19) & link_write_latch[0];
    wire activation_commit = unlock_second & unlock_pending & !bridge_active &
        (unlock_age < 16'd49999);
    wire relock_request = relock_frame | mcu_relock;
    wire ata_operational = drive_safe & bridge_active & !soft_reset & !diagnostic_pending;
    always @(posedge FPGA_CLK50 or negedge activation_resetn) begin
        if(!activation_resetn)begin
            bridge_active<=0;unlock_pending<=0;unlock_age<=0;fresh_fields<=0;
            safety_event_pending<=1;
        end else begin
            // Retain a short safety pulse until the control/FIFO processes
            // observe it. Otherwise old BSY could survive a sub-clock pulse.
            safety_event_pending<=0;
            if(unlock_pending)begin
                if(unlock_age==16'd49999)begin unlock_pending<=0;unlock_age<=0;end
                else unlock_age<=unlock_age+1'b1;
            end
            if(taskfile_write)begin
                case(ata_addr_latch)
                    1:fresh_fields[0]<=selected && dev_head==8'hb0;
                    2:fresh_fields[1]<=selected && dev_head==8'hb0;
                    3:fresh_fields[2]<=selected && dev_head==8'hb0;
                    4:fresh_fields[3]<=selected && dev_head==8'hb0;
                    5:fresh_fields[4]<=selected && dev_head==8'hb0;
                    // Changing to device0 invalidates the partial sequence.
                    6:if(!ata_write_latch[4])begin unlock_pending<=0;fresh_fields<=0;end
                    7:begin
                        fresh_fields<=0;
                        unlock_pending<=0;unlock_age<=0;
                        if(!bridge_active && unlock_first)unlock_pending<=1;
                        if(activation_commit)bridge_active<=1;
                        if(relock_frame)bridge_active<=0;
                    end
                    default: ;
                endcase
            end
            if(mcu_relock)begin bridge_active<=0;unlock_pending<=0;unlock_age<=0;fresh_fields<=0;end
        end
    end
    wire pio_data_read = selected & BUS_DMACKn & !BUS_CS0n & (BUS_DA==0) & status_reg[3] & (transfer_mode==0);
    wire dma_data_read = selected & !BUS_DMACKn & status_reg[3] & (transfer_mode==1);
    wire pio_reg_read = selected & BUS_DMACKn & ((!BUS_CS0n & (BUS_DA!=0)) | (!BUS_CS1n & (BUS_DA==6)));
    wire data_read = pio_data_read | dma_data_read;
    wire register_read = pio_reg_read;
    wire read_window = ata_operational & !BUS_DIORn & (data_read | register_read);
    // Input-direction OE observes every taskfile write, including DEVICE/HEAD
    // when the original GD-ROM is selected. This never drives Dreamcast data.
    wire input_window = drive_safe & !BUS_DIOWn &
        ((BUS_DMACKn & !BUS_CS1n & BUS_DA==6) | (!soft_reset &
        ((BUS_DMACKn & !BUS_CS0n) | (selected & !BUS_DMACKn & transfer_mode==2))));
    wire read_drive = ata_operational & (read_window | (read_owned & hold_count!=0)) & BUS_DIOWn;
    assign REQ_DATA_OEn = !(read_drive | input_window);
    assign DATA_DIR = read_drive;
    // BUS_DD is board-side of a separately gated directional transceiver.
    assign BUS_DD = read_drive ? (read_owned ? read_latch : read_data) : 16'bz;

    wire [15:0] tx_front, rx_front;
    wire [FIFO_AW:0] tx_count, rx_count;
    wire tx_full, rx_full, tx_empty, rx_empty;
    reg fifo_flush;
    wire clear_fifos = !drive_safe | soft_reset | safety_event_pending | fifo_flush;
    wire tx_push = link_wr & link_addr_latch==0 & tx_byte_high & !tx_full;
    wire host_data_write = ata_wr_end & ata_operational & selected & status_reg[3] &
        ((!ata_dma_latch & ata_cs0_latch & ata_addr_latch==0 & transfer_mode==0) | (ata_dma_latch & transfer_mode==2));
    wire rx_push = host_data_write & !rx_full & (transfer_words!=0);
    wire tx_pop = ata_rd_end & read_owned & read_was_data & !read_waiting & !tx_empty & ata_operational;
    wire rx_pop = link_rd_end & link_read_addr_latch==0 & rx_byte_high & !rx_empty;
    wire [15:0] tx_word = {link_write_latch,tx_low};
    bridge_fifo #(.AW(FIFO_AW)) tx_fifo(FPGA_CLK50,link_resetn,clear_fifos,tx_push,tx_word,tx_pop,tx_front,tx_count,tx_full,tx_empty);
    bridge_fifo #(.AW(FIFO_AW)) rx_fifo(FPGA_CLK50,link_resetn,clear_fifos,rx_push,ata_write_latch,rx_pop,rx_front,rx_count,rx_full,rx_empty);
    reg read_was_data, read_waiting;
    reg [1:0] read_ready_delay;
    reg [15:0] read_data;
    always @* begin
        read_data=16'hffff;
        if (data_read) read_data=tx_empty ? 16'hffff : tx_front;
        else if (!BUS_CS0n) case(BUS_DA)
            1:read_data={8'h00,error_reg}; 2:read_data={8'h00,sector_count};
            3:read_data={8'h00,lba0}; 4:read_data={8'h00,lba1};
            5:read_data={8'h00,lba2}; 6:read_data={8'h00,dev_head};
            7:read_data={8'h00,status_reg}; default:read_data=16'hffff;
        endcase
        else if (!BUS_CS1n && BUS_DA==6) read_data={8'h00,status_reg};
    end
    assign REQ_IORDY_LOW = ata_operational & selected & status_reg[3] &
        ((!BUS_DIORn & data_read & (tx_empty | read_waiting)) | (!BUS_DIOWn &
        ((transfer_mode==0 & BUS_DMACKn & !BUS_CS0n & BUS_DA==0) | (transfer_mode==2 & !BUS_DMACKn)) & rx_full));
    assign REQ_INTRQ_OEn = !(ata_operational & selected & !control_reg[1]);
    assign REQ_DMARQ_OEn = !(ata_operational & selected & (transfer_mode==1 | transfer_mode==2));
    assign REQ_INTRQ = ata_operational & selected & intrq_pending & !control_reg[1];
    // DMARQ drops after the synchronized read/write end, not on its leading
    // edge. read_owned and read_latch retain the final word through hold time.
    assign REQ_DMARQ = ata_operational & selected & status_reg[3] &
        ((transfer_mode==1 & !tx_empty) | (transfer_mode==2 & !rx_full));
    assign LINK_IRQ = FPGA_READY & (command_pending | transfer_done | fault | rx_count!=0);
    assign LINK_READY = FPGA_READY & LINK_WRn & LINK_RDn & (&wr_pipe) & (&rd_pipe);
    assign BIOS_WR_EN = bios_write_request & drive_safe & !soft_reset;

    reg [7:0] indexed_data;
    always @* begin
        indexed_data=0;
        case (index_reg)
            0:indexed_data={selected,command_pending,intrq_pending,fault,transfer_mode,status_reg[3],BIOS_RECOVERYn};
            1:indexed_data=command; 2:indexed_data=feature; 3:indexed_data=sector_count;
            4:indexed_data=lba0; 5:indexed_data=lba1; 6:indexed_data=lba2; 7:indexed_data=dev_head;
            8:indexed_data=control_reg; 9:indexed_data=status_reg; 10:indexed_data=error_reg;
            8'h15:indexed_data={7'b0,transfer_done};
            8'h16:indexed_data=transfer_words[7:0];
            8'h17:indexed_data=transfer_words[15:8];
            8'h18:indexed_data=transfer_words[23:16];
            8'h19:indexed_data={5'b0,diagnostic_pending,unlock_pending,bridge_active};
            8'h20:indexed_data=rx_count;
            8'h21:indexed_data=rx_count >> 8;
            8'h22:indexed_data=tx_count;
            8'h23:indexed_data=tx_count >> 8;
            8'h30:indexed_data={5'b0,bios_write_request,BIOS_BANK1,BIOS_BANK0};
            default:indexed_data=0;
        endcase
    end
    reg [7:0] link_read_data;
    always @* case(link_addr)
        0:link_read_data=rx_empty ? 8'hff : (rx_byte_high ? rx_front[15:8] : rx_front[7:0]);
        1:link_read_data={3'b0,fault,rx_full,tx_full,rx_empty,tx_empty};
        2:link_read_data=index_reg;
        3:link_read_data=indexed_data;
    endcase
    assign LINK_D = FPGA_READY & !LINK_RDn & LINK_WRn ? link_read_data : 8'bz;

    // BIOS bank is fixed per FPGA build until full console reset coordination
    // exists. Power-fail, MCU reset and ATA reset must not alter live ROM bank.
    // Initial FF state is loaded only by configuration. Factory build bank0.
    initial begin BIOS_BANK0=BIOS_INITIAL_BANK[0]; BIOS_BANK1=BIOS_INITIAL_BANK[1]; end

    always @(posedge FPGA_CLK50 or negedge link_resetn) begin
        if (!link_resetn) begin
            index_reg<=0;tx_byte_high<=0;rx_byte_high<=0;tx_low<=0;
            feature<=0;sector_count<=1;lba0<=1;lba1<=0;lba2<=0;dev_head<=8'ha0;
            command<=0;status_reg<=8'h40;error_reg<=1;control_reg<=0;
            command_pending<=0;intrq_pending<=0;fault<=0;transfer_mode<=0;transfer_done<=0;transfer_words<=0;
            diagnostic_pending<=0;
            bios_write_request<=0;read_owned<=0;read_latch<=0;hold_count<=0;
            read_was_data<=0;read_waiting<=0;read_ready_delay<=0;fifo_flush<=0;
        end else begin
            fifo_flush<=0;
            if (hold_count!=0) hold_count<=hold_count-1'b1;
            else if (BUS_DIORn) read_owned<=0;
            if (!drive_safe) begin read_owned<=0;hold_count<=0;bios_write_request<=0;read_waiting<=0;read_ready_delay<=0;end
            if (ata_rd_begin & read_window) begin
                read_latch<=read_data;read_owned<=1;read_was_data<=data_read;
                read_waiting<=data_read && tx_empty;read_ready_delay<=0;
                hold_count<=READ_HOLD_CYCLES;
                if (!BUS_CS0n & BUS_DA==7) intrq_pending<=0;
            end
            // While an owned read is active, continually reload hold timer.
            if (read_owned & !BUS_DIORn) hold_count<=READ_HOLD_CYCLES;
            if (tx_pop || rx_push) begin
                if (transfer_words>0) transfer_words<=transfer_words-1'b1;
                else fault<=1;
                if (transfer_words==1)begin
                    status_reg[3]<=0;transfer_mode<=0;transfer_done<=1;
                    // Receiving the last WRITE word is not media completion.
                    // Keep host busy until the MCU drains RX and commits SD.
                    if(rx_push)begin status_reg[7]<=1;intrq_pending<=0;end
                end
            end
            if (host_data_write && rx_full) fault<=1;
            if (read_owned && read_waiting && !BUS_DIORn && !tx_empty) begin
                if(read_ready_delay==0)begin read_latch<=tx_front;read_ready_delay<=2;end
                else if(read_ready_delay==1)begin read_waiting<=0;read_ready_delay<=0;end
                else read_ready_delay<=read_ready_delay-1'b1;
            end
            if (ata_rd_end && read_owned && read_waiting)begin fault<=1;read_waiting<=0;end
            if (link_rd_end && link_read_addr_latch==0 && !rx_empty) rx_byte_high<=~rx_byte_high;
            if (link_wr) case(link_addr_latch)
                0:begin
                    if (tx_full) fault<=1;
                    else begin tx_byte_high<=~tx_byte_high;if(!tx_byte_high)tx_low<=link_write_latch;end
                end
                2:index_reg<=link_write_latch;
                3:case(index_reg)
                    8'h10:begin
                        if(!diagnostic_pending)begin
                            if(link_write_latch[3] && transfer_words==0)begin status_reg<=link_write_latch & 8'hf7;fault<=1;end
                            else status_reg<=link_write_latch;
                        end
                    end
                    8'h11:error_reg<=link_write_latch;
                    8'h12:transfer_mode<=link_write_latch[1:0]==3 ? 0 : link_write_latch[1:0];
                    8'h13:begin intrq_pending<=link_write_latch[0] & !diagnostic_pending;if(link_write_latch[1])command_pending<=0;if(link_write_latch[2])transfer_done<=0;end
                    8'h14:if(link_write_latch[0])begin fifo_flush<=1;tx_byte_high<=0;rx_byte_high<=0;fault<=0;transfer_done<=0;transfer_words<=0;transfer_mode<=0;status_reg[3]<=0;end
                    8'h16:if(!status_reg[3])transfer_words[7:0]<=link_write_latch;
                    8'h17:if(!status_reg[3])transfer_words[15:8]<=link_write_latch;
                    8'h18:if(!status_reg[3])transfer_words[23:16]<=link_write_latch;
                    // Diagnostic result is explicitly supplied after MCU tests.
                    // Never invent a passing diagnostic code in the front end.
                    8'h1a:if(diagnostic_pending)begin
                        error_reg<={1'b0,link_write_latch[6:0]};status_reg<=8'h40;
                        sector_count<=1;lba0<=1;lba1<=0;lba2<=0;dev_head<=0;
                        diagnostic_pending<=0;command_pending<=0;intrq_pending<=0;
                        transfer_mode<=0;transfer_words<=0;transfer_done<=0;
                        fifo_flush<=1;tx_byte_high<=0;rx_byte_high<=0;
                    end
                    8'h30:bios_write_request<=link_write_latch[2]; // bank bits are read-only; see BIOS_INITIAL_BANK
                    default: ;
                endcase
                default: ;
            endcase
            if (ata_wr_end && drive_safe) begin
                // ATA shared command-block registers reach both devices,
                // even when the other drive is selected. Normal COMMAND executes
                // only for activated device1; 90h is received by both devices.
                // BSY or selected DRQ blocks command-block writes; DMACK must
                // be deasserted for taskfile reception. Control/SRST
                // below remains available independently of command-block BSY.
                if (taskfile_write) case(ata_addr_latch)
                    1:feature<=ata_write_latch[7:0];2:sector_count<=ata_write_latch[7:0];
                    3:lba0<=ata_write_latch[7:0];4:lba1<=ata_write_latch[7:0];5:lba2<=ata_write_latch[7:0];
                    6:dev_head<=ata_write_latch[7:0];
                    7:if(ata_write_latch[7:0]==8'h90 ||
                        (bridge_active && selected && ata_write_latch[7:0]!=8'hf0))begin
                        if(command_pending)fault<=1;
                        else begin
                            command<=ata_write_latch[7:0];command_pending<=1;status_reg<=8'h80;
                            error_reg<=0;intrq_pending<=0;transfer_mode<=0;transfer_done<=0;transfer_words<=0;
                            if(ata_write_latch[7:0]==8'h90)begin
                                diagnostic_pending<=1;dev_head<=0;
                                fifo_flush<=1;tx_byte_high<=0;rx_byte_high<=0;
                            end
                        end
                    end else if(bridge_active && selected && ata_write_latch[7:0]==8'hf0 && !relock_frame)begin
                        // Invalid vendor requests abort only after activation;
                        // a locked bridge remains absent rather than answering.
                        error_reg<=8'h04;status_reg<=8'h41;intrq_pending<=1;
                    end
                    default: ;
                endcase
                if (!ata_dma_latch && ata_cs1_latch && ata_addr_latch==6) control_reg<=ata_write_latch[7:0];
            end
            if(activation_commit || relock_request)begin
                feature<=0;sector_count<=1;lba0<=1;lba1<=0;lba2<=0;
                command<=0;status_reg<=8'h40;error_reg<=0;diagnostic_pending<=0;
                command_pending<=0;intrq_pending<=0;transfer_mode<=0;transfer_done<=0;transfer_words<=0;
                bios_write_request<=0;read_owned<=0;hold_count<=0;read_waiting<=0;read_ready_delay<=0;
                tx_byte_high<=0;rx_byte_high<=0;fifo_flush<=1;
            end
            // RESET has priority over link status writes, pending commands and
            // bus activity. SRST keeps the MCU link alive for recovery/status.
            if (!drive_safe || soft_reset || safety_event_pending) begin
                feature<=0;sector_count<=1;lba0<=1;lba1<=0;lba2<=0;dev_head<=8'ha0;
                command<=0;status_reg<=8'h40;error_reg<=1;
                command_pending<=0;intrq_pending<=0;transfer_mode<=0;transfer_done<=0;transfer_words<=0;
                diagnostic_pending<=0;
                bios_write_request<=0;read_owned<=0;hold_count<=0;read_waiting<=0;read_ready_delay<=0;
                tx_byte_high<=0;rx_byte_high<=0;
                if (!BUS_RESETn) control_reg<=0;
            end
        end
    end
endmodule

module bridge_fifo #(parameter AW=9)(
    input wire clk, resetn, clear,
    input wire push, input wire [15:0] din, input wire pop,
    output wire [15:0] dout,
    output reg [AW:0] count,
    output wire full, empty
);
    localparam DEPTH=1<<AW;
    (* ram_style="block" *) reg [15:0] memory [0:DEPTH-1];
    reg [15:0] memory_front, bypass_word;
    reg bypass_valid;
    reg [AW-1:0] write_ptr,read_ptr;
    assign full=count==DEPTH;
    assign empty=count==0;
    wire do_push=push && !full;
    wire do_pop=pop && !empty;
    wire [AW-1:0] next_read_ptr=do_pop ? read_ptr+1'b1 : read_ptr;
    // Synchronous memory read/write, kept out of asynchronous reset process.
    // Bypass covers empty push and replacing the last word on the same edge.
    always @(posedge clk) begin
        if(do_push && resetn && !clear) memory[write_ptr]<=din;
        memory_front<=memory[next_read_ptr];
    end
    assign dout=bypass_valid ? bypass_word : memory_front;
    always @(posedge clk or negedge resetn) begin
        if(!resetn)begin write_ptr<=0;read_ptr<=0;count<=0;bypass_valid<=0;bypass_word<=0;end
        else if(clear)begin write_ptr<=0;read_ptr<=0;count<=0;bypass_valid<=0;end
        else begin
            bypass_valid<=do_push && (empty || (do_pop && count==1));
            if(do_push && (empty || (do_pop && count==1)))bypass_word<=din;
            if(do_push)write_ptr<=write_ptr+1'b1;
            if(do_pop)read_ptr<=read_ptr+1'b1;
            case({do_push,do_pop})
                2'b10:count<=count+1'b1;
                2'b01:count<=count-1'b1;
                default: ;
            endcase
        end
    end
endmodule
`default_nettype wire
