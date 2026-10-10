`timescale 1ns/1ps
module tb_fifo;
    reg clk=0;always #10 clk=~clk;
    reg rst=0,clear=0,push=0,pop=0;reg[15:0] din=0;
    wire[15:0] dout;wire[3:0] count;wire full,empty;
    bridge_fifo #(.AW(3)) dut(clk,rst,clear,push,din,pop,dout,count,full,empty);
    reg[15:0] model[0:1023];integer wp=0,rp=0,n=0,k=0,checks=0;
    reg accepted_push,accepted_pop;
    task cycle(input wr,input rd,input[15:0] data);
        begin
            @(negedge clk);push=wr;pop=rd;din=data;
            accepted_push=wr && n<8;accepted_pop=rd && n>0;
            if(accepted_pop)begin
                if(dout!==model[rp])$fatal(1,"FIFO order mismatch before pop");rp=rp+1;n=n-1;
            end
            if(accepted_push)begin model[wp]=data;wp=wp+1;n=n+1;end
            @(posedge clk);#1;
            checks=checks+1;
            if(count!==n || full!==(n==8) || empty!==(n==0))$fatal(1,"FIFO count/flag mismatch");
            if(n>0 && dout!==model[rp])$fatal(1,"FIFO front mismatch");
        end
    endtask
    initial begin
        #25;rst=1;
        for(k=0;k<8;k=k+1)cycle(1,0,k);
        cycle(1,0,16'hffff); // Full overflow blocked.
        cycle(1,1,16'haaaa); // Full pop accepted; incoming word rejected.
        for(k=0;k<7;k=k+1)cycle(0,1,0);
        cycle(0,1,0); // Empty underflow blocked.
        cycle(1,0,16'hbeef);cycle(1,1,16'hcafe);cycle(0,1,0);
        // Repeated wrap-around and simultaneous one-word replacement.
        for(k=0;k<500;k=k+1)cycle((k%3)!=0,(k%5)!=0,k+16'h1000);
        @(negedge clk);clear=1;push=0;pop=0;@(posedge clk);#1;
        if(count!==0 || !empty || full)$fatal(1,"clear failed");
        $display("PASS: %0d FIFO scoreboarding cycles",checks);$finish;
    end
endmodule
