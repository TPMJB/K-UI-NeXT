`timescale 1ns/1ps
// Independent wire-level review of opt-in activation and EXECUTE DIAGNOSTIC.
// The test uses the public ATA/link interfaces, not hierarchical DUT state.
module tb_activation;
    reg clk=0; always #10 clk=~clk;
    tri [7:0] ld; reg [7:0] lwrite=0; reg ldrive=0;
    assign ld=ldrive ? lwrite : 8'hzz;
    reg wrn=1, rdn=1; reg [1:0] la=0;
    wire lirq, lready, fready;
    reg frst=0, arm=0, safe=0, pfail=0;
    tri [15:0] dd; reg [15:0] hostword=0; reg hostdrive=0;
    assign dd=hostdrive ? hostword : 16'hzzzz;
    reg [2:0] da=0;
    reg cs0=1, cs1=1, dior=1, diow=1, rst=0, dmack=1;
    wire oe, dir, wait_low, irq, drq, irqoe, drqoe, b0, b1, bwe;
    reg recovery=1;
    bridge #(.FIFO_AW(9)) dut(
        clk,ld,wrn,rdn,la[0],la[1],lirq,lready,frst,arm,safe,pfail,
        dd,da,cs0,cs1,dior,diow,rst,dmack,oe,dir,wait_low,irq,drq,
        irqoe,drqoe,fready,b0,b1,bwe,recovery
    );
    integer checks=0, failures=0, pulse_case;
    reg [7:0] val;
    reg [15:0] word;

    task check(input condition, input [511:0] message);
        begin
            checks=checks+1;
            if(condition !== 1'b1) begin
                failures=failures+1;
                $display("FAIL %0t: %0s",$time,message);
            end
        end
    endtask
    task quiet;
        begin
            check(dir===0,"no ATA data output direction while absent");
            check(wait_low===0 && irq===0 && drq===0,
                  "absent bridge never requests IORDY/INTRQ/DMARQ");
            check(irqoe===1 && drqoe===1,"absent bridge relinquishes output ownership");
            if(!hostdrive) check(dd===16'hzzzz,"absent bridge leaves DATA high impedance");
        end
    endtask
    task lw(input [1:0] addr, input [7:0] data);
        begin
            wait(lready); la=addr; lwrite=data; ldrive=1;
            #80; wrn=0; #120; wrn=1; #20; ldrive=0; #140;
        end
    endtask
    task lr(input [1:0] addr, output [7:0] data);
        begin
            wait(lready); la=addr; #80; rdn=0; #100; data=ld;
            #20; rdn=1; #160;
        end
    endtask
    task iw(input [7:0] idx, input [7:0] data);
        begin lw(2,idx); lw(3,data); end
    endtask
    task ir(input [7:0] idx, output [7:0] data);
        begin lw(2,idx); lr(3,data); end
    endtask
    task hw(input [2:0] addr, input [7:0] data);
        begin
            da=addr; cs0=0; hostword={8'h00,data}; hostdrive=1;
            #80; diow=0; #200;
            check(dir===0 && dd==={8'h00,data},"ATA write is input-only without contention");
            #60; diow=1; #20; hostdrive=0; #220; cs0=1;
        end
    endtask
    task hc(input [7:0] data);
        begin
            da=6; cs1=0; hostword={8'h00,data}; hostdrive=1;
            #80; diow=0; #260; diow=1; #20; hostdrive=0; #220; cs1=1;
        end
    endtask
    task absent_read(input [2:0] addr);
        begin
            da=addr; cs0=0; #80; dior=0; #200;
            quiet(); check(oe===1,"absent ATA read never enables data transceiver");
            #100; dior=1; #220; cs0=1;
        end
    endtask
    task active_read(input [2:0] addr, output [15:0] data);
        begin
            da=addr; cs0=0; #80; dior=0; #200; data=dd;
            check(oe===0 && dir===1,"activated device1 owns selected register read");
            #100; dior=1; #220; cs0=1;
        end
    endtask
    task fresh_fields(input [7:0] f, input [7:0] sc, input [7:0] lo,
                      input [7:0] mid, input [7:0] hi);
        begin hw(1,f); hw(2,sc); hw(3,lo); hw(4,mid); hw(5,hi); end
    endtask
    task first_frame;
        begin
            hw(6,8'hb0); fresh_fields(8'h4b,8'h55,8'h49,8'h01,8'ha5); hw(7,8'hf0);
        end
    endtask
    task second_frame;
        begin
            hw(6,8'hb0); fresh_fields(8'hb4,8'haa,8'hb6,8'hfe,8'h5a); hw(7,8'hf0);
        end
    endtask
    task activate;
        begin
            first_frame(); ir(8'h19,val);
            check(val==8'h02,"first fresh key produces partial state only");
            second_frame(); ir(8'h19,val);
            check(val==8'h01,"second fresh key activates within real 1 ms window");
        end
    endtask
    task reset_all;
        begin
            hostdrive=0; ldrive=0; cs0=1; cs1=1; dior=1; diow=1;
            wrn=1; rdn=1; dmack=1; frst=0; rst=0;
            #100; pfail=1; arm=1; safe=1; recovery=1; frst=1; rst=1; #180;
            ir(8'h19,val); check(val===0,"startup and hardware reset lock activation");
        end
    endtask
    task diagnostic_start(input [7:0] device);
        begin
            hw(6,device); hw(7,8'h90); ir(8'h19,val);
            check(val[2]===1,"90h creates explicit diagnostic-pending state");
            ir(1,val); check(val==8'h90,"90h is forwarded to MCU command mailbox");
            ir(0,val); check(val[6]===1,"diagnostic mailbox requests MCU service");
            ir(9,val); check(val==8'h80,"diagnostic remains BSY while MCU tests run");
            ir(7,val); check(val==0,"90h clears DEVICE/HEAD immediately");
            quiet(); absent_read(7);
        end
    endtask
    task diagnostic_complete(input [7:0] result);
        begin
            iw(8'h1a,result); ir(8'h19,val);
            check(val[2]===0,"explicit diagnostic result completes 90h");
            ir(9,val); check(val==8'h40,"diagnostic completion posts DRDY without DRQ or ERR");
            ir(10,val); check(val==(result & 8'h7f),"device1 diagnostic result clears reserved device0 failure bit");
            ir(3,val); check(val==1,"diagnostic signature Sector Count is 1");
            ir(4,val); check(val==1,"diagnostic signature LBA low is 1");
            ir(5,val); check(val==0,"diagnostic signature LBA middle is 0");
            ir(6,val); check(val==0,"diagnostic signature LBA high is 0");
            ir(7,val); check(val==0,"diagnostic completion posts zero DEVICE/HEAD signature");
            ir(0,val); check(val[6:5]===0,"diagnostic completes mailbox without pending interrupt");
            quiet();
        end
    endtask
    task short_safety_pulse(input pulse_arm);
        begin
            @(negedge clk); #2;
            if(pulse_arm) arm=0; else safe=0;
            #2; quiet();
            if(pulse_arm) arm=1; else safe=1;
            #180;
        end
    endtask
    task recovered_idle;
        begin
            ir(8'h19,val); check(val===0,"short safety loss clears active, partial and diagnostic state");
            ir(9,val); check(val==8'h40,"short safety loss clears stale BSY and DRQ on next clock");
            ir(0,val); check(val[6:5]===0,"short safety loss clears command mailbox and interrupt");
            ir(1,val); check(val==0,"short safety loss clears stale command opcode");
            ir(8'h20,val); check(val==0,"short safety loss flushes stale RX FIFO");
            ir(8'h22,val); check(val==0,"short safety loss flushes stale TX FIFO");
            quiet(); activate(); active_read(7,word);
            check(word==16'h0040,"fresh keys recover usable idle status without firmware cleanup");
        end
    endtask

    initial begin
        reset_all(); quiet(); hw(6,8'hb0); absent_read(7); absent_read(0);
        // Firmware cannot turn activation on through the MCU-side register.
        iw(8'h19,8'hfe); ir(8'h19,val); check(val===0,"MCU cannot set active or partial bits");
        iw(8'h19,1); ir(8'h19,val); check(val===0,"MCU activation bit is relock-only");
        // Even prepared transfers and interrupt writes remain electrically absent.
        iw(8'h16,1); iw(8'h12,2); iw(8'h10,8'h48); iw(8'h13,1);
        absent_read(7); absent_read(0); quiet();
        reset_all();
        // Shared taskfile snooping works while GD-ROM device0 is selected.
        hw(1,8'h33); hw(2,8'h12); hw(3,8'h56); hw(4,8'h78); hw(5,8'h9a);
        ir(2,val); check(val==8'h33,"locked bridge snoops shared Features input");
        ir(3,val); check(val==8'h12,"locked bridge snoops shared Sector Count input");
        ir(4,val); check(val==8'h56,"locked bridge snoops shared LBA0 input");
        ir(5,val); check(val==8'h78,"locked bridge snoops shared LBA1 input");
        ir(6,val); check(val==8'h9a,"locked bridge snoops shared LBA2 input");
        hw(7,8'h20); ir(0,val); check(val[6]===0,"ordinary device0 command is not forwarded");
        hw(6,8'hb0); hw(7,8'h20); ir(0,val);
        check(val[6]===0,"ordinary device1 command is ignored until activated");
        hw(6,8'ha0); fresh_fields(8'h4b,8'h55,8'h49,8'h01,8'ha5);
        hw(6,8'hb0); hw(7,8'hf0); ir(8'h19,val);
        check(val===0,"keys written with device0 selected cannot become fresh device1 keys");
        // Wrong selection and malformed keys never unlock or acquire output.
        fresh_fields(8'h4b,8'h55,8'h49,8'h01,8'ha5); hw(6,8'hb1); hw(7,8'hf0);
        ir(8'h19,val); check(val===0,"vendor frame requires exact DEVICE/HEAD B0h");
        hw(6,8'hb0); fresh_fields(8'h4b,8'h55,8'h49,8'h01,8'ha4); hw(7,8'hf0);
        ir(8'h19,val); check(val===0,"one malformed key byte rejects frame");
        second_frame(); ir(8'h19,val); check(val===0,"second key alone cannot activate");
        // Reusing old matching bytes fails: all five fields must be fresh.
        fresh_fields(8'h4b,8'h55,8'h49,8'h01,8'ha5); hw(7,8'h20);
        hw(1,8'h4b); hw(2,8'h55); hw(3,8'h49); hw(4,8'h01); hw(7,8'hf0);
        ir(8'h19,val); check(val===0,"stale LBA2 invalidates otherwise matching first key");
        first_frame(); hw(7,8'hf0); ir(8'h19,val);
        check(val===0,"command without five new bytes cancels partial sequence");
        first_frame(); hw(7,8'h20); second_frame(); ir(8'h19,val);
        check(val===0,"ordinary intervening command cancels partial sequence");
        first_frame(); hw(6,8'ha0); second_frame(); ir(8'h19,val);
        check(val===0,"selecting GD-ROM device0 cancels partial sequence");
        first_frame(); #1100000; ir(8'h19,val);
        check(val===0,"partial key expires after real 1 ms interval");
        second_frame(); ir(8'h19,val); check(val===0,"expired first key cannot authorize second key");
        quiet(); absent_read(7);
        first_frame(); #990000; second_frame(); ir(8'h19,val);
        check(val===1,"fresh second key just inside real 1 ms interval is accepted");
        iw(8'h19,1); first_frame(); @(negedge clk); #2; safe=0; #2; safe=1;
        #160; second_frame(); ir(8'h19,val);
        check(val===0,"sub-clock safety pulse also invalidates partial key sequence");
        // Fresh valid keys expose an ordinary idle register and command mailbox.
        activate(); active_read(7,word); check(word==16'h0040,"activation posts idle DRDY status");
        hw(7,8'h20); ir(1,val); check(val==8'h20,"activated ordinary ATA command reaches MCU");
        ir(0,val); check(val[6]===1,"activated ordinary command sets pending mailbox");
        iw(8'h13,2); iw(8'h10,8'h40);
        fresh_fields(8'h4c,8'h4f,8'h43,8'h4b,0); hw(7,8'hf0); ir(8'h19,val);
        check(val===0,"fresh host LOCK vendor frame disables activation");
        absent_read(7); activate(); iw(8'h19,1); ir(8'h19,val);
        check(val===0,"MCU relock disables active bridge"); absent_read(7);
        // A short safety pulse must invalidate activation without a clock edge.
        for(pulse_case=0;pulse_case<6;pulse_case=pulse_case+1) begin
            activate(); @(negedge clk); #2;
            case(pulse_case)
                0:arm=0; 1:safe=0; 2:rst=0; 3:frst=0; 4:pfail=0; 5:recovery=0;
            endcase
            #2; quiet();
            case(pulse_case)
                0:arm=1; 1:safe=1; 2:rst=1; 3:frst=1; 4:pfail=1; 5:recovery=1;
            endcase
            #180; ir(8'h19,val); check(val===0,"sub-clock safety pulse permanently relocks bridge");
            hw(6,8'hb0); absent_read(7);
        end
        activate(); hc(4); #100; hc(0); ir(8'h19,val);
        check(val===0,"ATA SRST clears activation until fresh host keys");
        hw(6,8'hb0); absent_read(7);
        // 90h is device-independent even with opt-in activation still locked.
        reset_all(); diagnostic_start(8'ha0);
        iw(8'h10,8'h40); iw(8'h13,3); ir(8'h19,val);
        check(val==8'h04,"ordinary MCU status/IRQ writes cannot finish locked diagnostic");
        ir(9,val); check(val==8'h80,"diagnostic BSY survives ordinary MCU status write");
        diagnostic_complete(8'h01); ir(8'h19,val);
        check(val===0,"successful diagnostic never activates bridge");
        hw(6,8'hb0); absent_read(1); absent_read(7);
        diagnostic_start(8'hb0); diagnostic_complete(8'h85);
        ir(8'h19,val); check(val===0,"failed diagnostic never activates bridge");
        // nIEN does not suppress diagnostics or permit an IRQ on completion.
        hc(2); diagnostic_start(8'hb0); diagnostic_complete(8'h01);
        check(irq===0 && irqoe===1,"nIEN diagnostic neither asserts nor owns INTRQ");
        // Active diagnostics temporarily become electrically absent and retain
        // activation only; the host must explicitly reselect device1 afterward.
        activate(); iw(8'h13,1); diagnostic_start(8'hb0); ir(8'h19,val);
        check(val==8'h05,"active diagnostic preserves opt-in state while pending");
        iw(8'h10,8'h48); iw(8'h13,1); ir(9,val);
        check(val==8'h80,"active diagnostic rejects ordinary status completion");
        diagnostic_complete(8'h01); ir(8'h19,val);
        check(val==8'h01,"active diagnostic completion preserves activation");
        hw(6,8'hb0); active_read(1,word); check(word==16'h0001,"reselected device1 exposes diagnostic result");
        active_read(2,word); check(word==16'h0001,"reselected diagnostic signature is visible");
        check(irq===0 && irqoe===1,"diagnostic completion respects nIEN on reselect");
        // A busy ordinary command rejects 90h rather than replacing its mailbox.
        hc(0); hw(7,8'h20); hw(7,8'h90); ir(1,val);
        check(val==8'h20,"BSY rejects diagnostic without overwriting current command");
        ir(8'h19,val); check(val==8'h01,"rejected diagnostic does not set pending state");
        iw(8'h13,2); iw(8'h16,1); iw(8'h10,8'h48); hw(7,8'h90);
        ir(8'h19,val); check(val==8'h01,"selected DRQ rejects diagnostic");
        iw(8'h14,1); iw(8'h10,8'h40); iw(8'h19,1); quiet();
        // A sub-clock BUS_SAFE loss during a pending ordinary command must
        // retain cleanup until the next clock, including both stale FIFOs.
        reset_all(); activate(); lw(0,8'haa); lw(0,8'h55);
        hw(7,8'h30); iw(8'h13,2); iw(8'h16,1); iw(8'h10,8'h48); hw(0,8'hc7);
        iw(8'h10,8'h40); hw(7,8'h20);
        ir(0,val); check(val[6]===1,"busy safety test starts with pending ordinary command");
        ir(9,val); check(val==8'h80,"busy safety test starts with BSY set");
        ir(8'h20,val); check(val==1,"busy safety test contains a stale RX word");
        ir(8'h22,val); check(val==1,"busy safety test contains a stale TX word");
        short_safety_pulse(0); recovered_idle();
        // Diagnostic lockout also must recover from a short ARM pulse without
        // requiring its now-aborted result to be supplied by firmware.
        reset_all(); diagnostic_start(8'ha0); lw(0,8'hee); lw(0,8'h11);
        ir(8'h22,val); check(val==1,"locked diagnostic test contains stale MCU TX word");
        short_safety_pulse(1); recovered_idle();
        if(failures) begin
            $display("FAIL: %0d of %0d activation/diagnostic assertions",failures,checks);
            $fatal(1);
        end
        $display("PASS: %0d independent activation/diagnostic assertions",checks);
        $finish;
    end
    initial begin #5000000; $fatal(1,"activation/diagnostic regression timeout"); end
endmodule
