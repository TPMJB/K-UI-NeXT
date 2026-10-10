`timescale 1ns/1ps
module tb_bridge;
    reg clk=0;always #10 clk=~clk;
    tri [7:0] ld;reg[7:0] lwrite=0;reg ldrive=0;
    assign ld=ldrive ? lwrite : 8'hzz;
    reg wrn=1,rdn=1;reg[1:0] la=0;
    wire lirq,lready,fready;
    reg frst=0,arm=0,safe=0,pfail=0;
    tri [15:0] dd;reg[15:0] hostword=0;reg hostdrive=0;
    assign dd=hostdrive ? hostword : 16'hzzzz;
    reg[2:0] da=0;reg cs0=1,cs1=1,dior=1,diow=1,rst=0,dmack=1;
    wire oe,dir,wait_low,irq,drq,irqoe,drqoe,b0,b1,bwe;reg recovery=1;
    bridge #(.FIFO_AW(3)) dut(clk,ld,wrn,rdn,la[0],la[1],lirq,lready,frst,arm,safe,pfail,dd,da,cs0,cs1,dior,diow,rst,dmack,oe,dir,wait_low,irq,drq,irqoe,drqoe,fready,b0,b1,bwe,recovery);
    integer checks=0;reg[7:0] val;reg[15:0] word;
    task check(input condition,input[255:0] message);
        begin checks=checks+1;if(condition!==1'b1)begin $display("FAIL %0t: %0s",$time,message);$fatal(1);end end
    endtask
    task lw(input[1:0] addr,input[7:0] data);
        begin wait(lready);la=addr;lwrite=data;ldrive=1;#80;wrn=0;#120;wrn=1;#20;ldrive=0;#140;end
    endtask
    task lr(input[1:0] addr,output[7:0] data);
        begin wait(lready);la=addr;#80;rdn=0;#100;data=ld;#20;rdn=1;#160;end
    endtask
    task iw(input[7:0] idx,input[7:0] data);begin lw(2,idx);lw(3,data);end endtask
    task ir(input[7:0] idx,output[7:0] data);begin lw(2,idx);lr(3,data);end endtask
    task hw(input[2:0] addr,input[15:0] data);
        begin da=addr;cs0=0;hostword=data;hostdrive=1;#80;diow=0;#260;diow=1;#20;hostdrive=0;#220;cs0=1;end
    endtask
    task activate;
        begin
            hw(6,16'h00b0);
            hw(1,16'h004b);hw(2,16'h0055);hw(3,16'h0049);hw(4,16'h0001);hw(5,16'h00a5);hw(7,16'h0000);
            hw(1,16'h00b4);hw(2,16'h00aa);hw(3,16'h00b6);hw(4,16'h00fe);hw(5,16'h005a);hw(7,16'h0000);
        end
    endtask
    task hc(input[7:0] data);
        begin da=6;cs1=0;hostword={8'h00,data};hostdrive=1;#80;diow=0;#260;diow=1;#20;hostdrive=0;#220;cs1=1;end
    endtask
    task hr(input[2:0] addr,output[15:0] data);
        begin da=addr;cs0=0;#80;dior=0;#200;data=dd;check(!oe && dir,"selected read owns output");#99;check(dd===data,"ATA data valid to DIOR rise");#1;dior=1;#1;check(oe && dd===16'hzzzz,"PIO read releases on DIOR rise (t6z)");#219;cs0=1;end
    endtask
    initial begin
        #75;check(oe && !dir && !irq && !drq,"reset is isolated");
        pfail=1;frst=1;rst=1;#160;
        check(fready,"READY independent of BUS_SAFE");check(oe,"hardware-safe stays isolated");
        ir(8'h20,val);check(val===0,"small FIFO RX count zero extends");
        ir(8'h22,val);check(val===0,"small FIFO TX count zero extends");
        // No whole-console held-reset hardware exists: factory bank0 is immutable.
        iw(8'h30,8'h03);check(!b0 && !b1,"factory bank0 blocks unsafe bank write");
        arm=1;safe=1;#80;activate;hw(6,16'h00a0);iw(8'h30,8'h00);check(!b0 && !b1,"factory bank0 remains locked after arm");
        da=7;cs0=0;dior=0;#200;check(oe && dd===16'hzzzz,"device0 read never drives");dior=1;#220;cs0=1;
        dmack=0;hw(3,16'h00cc);dmack=1;ir(4,val);check(val==1,"DMACK blocks shared taskfile write");
        hw(6,16'h00b0);check(dut.selected,"shared DEVICE/HEAD selects device1");
        hr(7,word);check(word==16'h0040,"reset status is DRDY");
        dmack=0;da=7;cs0=0;dior=0;#180;check(oe && dd===16'hzzzz,"DMACK blocks PIO register read");dior=1;cs0=1;dmack=1;#220;
        hw(3,16'h005a);hw(7,16'h0020);check(lirq,"command alerts MCU");
        ir(1,val);check(val==8'h20,"command mailbox captures opcode");
        ir(4,val);check(val==8'h5a,"mailbox captures LBA");
        hw(3,16'h00cc);ir(4,val);check(val==8'h5a,"BSY blocks taskfile writes");
        hw(6,16'h00a0);ir(7,val);check(val==8'hb0,"BSY blocks device-head changes");
        hr(7,word);check(word==16'h0080,"command immediately sets BSY");
        // PIO read: byte link packs low byte first, final word is held.
        lw(0,8'h34);lw(0,8'h12);lw(0,8'h78);lw(0,8'h56);
        iw(8'h13,8'h03);iw(8'h16,2);iw(8'h10,8'h48);
        check(irq && !irqoe,"explicit completion requests INTRQ");
        hw(6,16'h00a0);ir(7,val);check(val==8'hb0,"selected DRQ blocks head changes");
        hc(8'h02);check(!irq && irqoe,"nIEN removes interrupt ownership");hc(0);check(irq && !irqoe,"nIEN restore keeps pending interrupt");
        hr(0,word);check(word==16'h1234,"PIO first FIFO word");
        hr(0,word);check(word==16'h5678,"PIO final FIFO word");
        check(!dut.status_reg[3] && dut.tx_empty,"final read clears DRQ");check(dut.transfer_done && lirq,"word count records transfer completion");
        // RX FIFO low/high unpacking.
        iw(8'h16,1);iw(8'h10,8'h48);hw(0,16'hbeef);lr(0,val);check(val==8'hef,"RX low byte first");lr(0,val);check(val==8'hbe,"RX high byte second");check(dut.rx_empty,"RX advances once per word");
        // DMA read: ownership persists when DMACK rises on final DIOR edge.
        lw(0,8'had);lw(0,8'hde);iw(8'h12,1);iw(8'h16,1);iw(8'h10,8'h48);
        check(drq && !drqoe,"DMA asserts only with data ready");
        dmack=0;#80;dior=0;#200;check(dd===16'hdead,"DMA data word");check(!oe && dir,"DMA owns read data");#100;dior=1;dmack=1;#15;check(dd===16'hdead,"DMA final word held");#205;check(!drq && drqoe && dut.tx_empty,"DMA releases OE after final strobe");
        // FIFO drain is not command completion: stream a second word later.
        iw(8'h14,1);lw(0,8'h11);lw(0,8'h11);iw(8'h16,2);iw(8'h10,8'h48);
        hr(0,word);check(word==16'h1111,"streamed first word");
        check(dut.status_reg[3] && !dut.transfer_done && dut.tx_empty,"FIFO drain preserves pending transfer");
        // Starved PIO read holds IORDY; filling FIFO supplies data before release.
        da=0;cs0=0;dior=0;#120;check(wait_low,"empty PIO read stretches IORDY");
        lw(0,8'hca);lw(0,8'hfe);#100;check(!wait_low && dd===16'hfeca,"late FIFO fill supplies held read");
        dior=1;#220;cs0=1;check(dut.tx_empty,"late filled read consumes one word");
        // Software reset can be cleared even though ATA responder is inactive.
        hc(4);check(!dut.selected && oe,"SRST drops ownership");hc(0);activate;hr(7,word);check(word==16'h0040,"SRST deassert recovery");
        // Kill independent of sampled reset and strobe; no stale IRQ/DMA.
        lw(0,8'h55);lw(0,8'haa);iw(8'h16,1);iw(8'h10,8'h48);da=0;cs0=0;dior=0;#180;check(!oe,"PIO data before power kill");safe=0;#1;check(oe && dd===16'hzzzz && !irq && !drq,"BUS_SAFE kill immediately isolates");dior=1;cs0=1;#220;
        safe=1;rst=0;#100;check(oe && !irq && !drq,"ATA reset releases outputs");check(!b0 && !b1,"ATA reset preserves BIOS bank");rst=1;#100;
        frst=0;#80;check(!b0 && !b1 && !bwe,"MCU reset preserves bank blocks write");frst=1;#140;
        recovery=0;hw(6,16'hb0);da=7;cs0=0;dior=0;#180;check(oe && !irq && !drq && !bwe,"physical recovery disables bridge");dior=1;cs0=1;recovery=1;#120;
        pfail=0;#1;check(!fready && oe && !b0 && !b1,"early power fail preserves ROM bank");
        $display("PASS: %0d bridge assertions",checks);$finish;
    end
    initial begin #200000;$fatal(1,"simulation timeout");end
endmodule
