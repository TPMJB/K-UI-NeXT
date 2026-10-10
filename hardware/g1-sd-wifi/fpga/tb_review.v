`timescale 1ns/1ps
module tb_review;
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
    integer checks=0; integer failures=0;reg[7:0] val;reg[15:0] word;
    task check(input condition,input[255:0] message);
        begin checks=checks+1;if(condition!==1'b1)begin $display("FAIL %0t: %0s",$time,message);$display("Continuing independent review after failure");failures=failures+1;end end
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
            hw(1,16'h004b);hw(2,16'h0055);hw(3,16'h0049);hw(4,16'h0001);hw(5,16'h00a5);hw(7,16'h00f0);
            hw(1,16'h00b4);hw(2,16'h00aa);hw(3,16'h00b6);hw(4,16'h00fe);hw(5,16'h005a);hw(7,16'h00f0);
        end
    endtask
    task hc(input[7:0] data);
        begin da=6;cs1=0;hostword={8'h00,data};hostdrive=1;#80;diow=0;#260;diow=1;#20;hostdrive=0;#220;cs1=1;end
    endtask
    task hr(input[2:0] addr,output[15:0] data);
        begin da=addr;cs0=0;#80;dior=0;#200;data=dd;check(!oe && dir,"selected read owns output");#100;dior=1;#15;check(dd===data,"ATA data hold after DIOR");#205;cs0=1;end
    endtask
    // Written independently after review: exercises legal host transactions,
    // observes wire-visible status rather than matching the implementation.
    initial begin
        #75;pfail=1;frst=1;rst=1;arm=1;safe=1;#160;activate;hw(6,16'h00a0);
        // Both ATA devices receive the non-command taskfile writes.
        // Taskfile parameters may precede the final DEVICE/HEAD selection.
        hw(1,16'h0033);hw(2,16'h0002);hw(3,16'h005a);
        hw(4,16'h00c3);hw(5,16'h0096);hw(6,16'h00f0);
        hw(7,16'h0020);
        ir(2,val);check(val==8'h33,"shared Features capture");
        ir(3,val);check(val==8'h02,"shared Sector Count capture");
        ir(4,val);check(val==8'h5a,"shared LBA low capture");
        ir(5,val);check(val==8'hc3,"shared LBA mid capture");
        ir(6,val);check(val==8'h96,"shared LBA high capture");
        // Recover/finish command without reset, then accept a one-word write.
        iw(8'h13,2);iw(8'h10,8'h40);
        hw(7,16'h0030);iw(8'h13,2);iw(8'h16,1);iw(8'h10,8'h48);
        hw(0,16'hbeef);
        hr(7,word);check(word[7] && !word[3],"PIO write waits for media commit");
        // Merely draining RX is not durable completion.
        lr(0,val);check(val==8'hef,"PIO write captured low byte");
        lr(0,val);check(val==8'hbe,"PIO write captured high byte");
        hr(7,word);check(word[7] && !word[3],"drain preserves pending write");
        // MCU completes after storage has actually committed the write.
        iw(8'h10,8'h40);hr(7,word);check(word==16'h0040,"MCU completes media write");
        // DMA write follows the same persistence rule and releases request.
        iw(8'h14,1);iw(8'h10,8'h40);hw(7,16'h00ca);
        iw(8'h13,2);iw(8'h12,2);iw(8'h16,1);iw(8'h10,8'h48);
        hostword=16'h1234;hostdrive=1;dmack=0;#80;
        diow=0;#260;diow=1;#20;hostdrive=0;dmack=1;#220;
        hr(7,word);check(word[7] && !word[3],"DMA write waits for media commit");
        check(!drq && drqoe,"completed DMA data releases request");
        if(failures)begin $display("FAIL: %0d of %0d independent review assertions",failures,checks);$fatal(1);end
        $display("PASS: %0d independent review assertions",checks);$finish;
    end
    initial begin #200000;$fatal(1,"simulation timeout");end
endmodule
