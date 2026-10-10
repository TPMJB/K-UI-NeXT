// SPDX-License-Identifier: GPL-3.0-only
// Headless orchestration of official Freerouting v1.9.0 public core APIs.
// No router algorithms or design-rule checks are patched. Native KiCad DRC
// and circuit parity remain required after every imported session.
import app.freerouting.board.*;
import app.freerouting.designforms.specctra.DsnFile;
import app.freerouting.designforms.specctra.SpecctraSesFileWriter;
import app.freerouting.datastructures.*;
import app.freerouting.interactive.*;
import app.freerouting.autoroute.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public final class Headless19Ripup {
  static final class Deadline implements Stoppable {
    volatile boolean stopped=false;
    final long expires;
    Deadline(int seconds) { expires=System.nanoTime()+seconds*1000000000L; }
    public void request_stop() { stopped=true; }
    public boolean is_stop_requested() { return stopped || System.nanoTime()>=expires; }
  }
  static int incomplete(RoutingBoard b) { return new RatsNest(b,Locale.US).incomplete_count(); }
  static void checkpoint(RoutingBoard b,Path path,String design) throws Exception {
    Path temp=path.resolveSibling(path.getFileName().toString()+".writing");
    try(OutputStream out=Files.newOutputStream(temp)) {
      if(!SpecctraSesFileWriter.write(b,out,design)) throw new IOException("SES writer failed");
    }
    Files.move(temp,path,StandardCopyOption.REPLACE_EXISTING);
  }
  // This is the official v1.9 BatchAutorouter.autoroute_item API recipe,
  // with its display-only airline calculation omitted. The official maze
  // engine and control/rules code remain unchanged.
  static AutorouteEngine.AutorouteResult routeWithRipup(RoutingBoard b, Pin pin,
      Settings settings, int viaCosts, int pass, Deadline stop, int millis) {
    int netNo=pin.get_net_no(0);
    boolean plane=b.rules.nets.get(netNo).contains_plane();
    Set<Item> connected=pin.get_connected_set(netNo);
    Set<Item> unconnected=pin.get_unconnected_set(netNo);
    if(unconnected.isEmpty()) return AutorouteEngine.AutorouteResult.ALREADY_CONNECTED;
    if(plane) for(Item item:connected) {
      if(item instanceof ConductionArea) return AutorouteEngine.AutorouteResult.ALREADY_CONNECTED;
    }
    AutorouteControl control=new AutorouteControl(b,netNo,settings,viaCosts,
        settings.autoroute_settings.get_trace_cost_arr());
    control.ripup_allowed=true;
    control.ripup_costs=settings.autoroute_settings.get_start_ripup_costs()*pass;
    control.remove_unconnected_vias=false;
    SortedSet<Item> ripped=new TreeSet<Item>();
    AutorouteEngine engine=b.init_autoroute(netNo,control.trace_clearance_class_no,
        stop,new TimeLimit(millis),false);
    AutorouteEngine.AutorouteResult result=engine.autoroute_connection(
        plane?connected:unconnected,plane?unconnected:connected,control,ripped);
    if(result==AutorouteEngine.AutorouteResult.ROUTED) {
      b.opt_changed_area(new int[0],null,settings.get_trace_pull_tight_accuracy(),
          control.trace_costs,stop,1000);
    }
    return result;
  }
  public static void main(String[] args) throws Exception {
    if(args.length<2) throw new IllegalArgumentException("Headless19Ripup input.dsn output.ses [maxPasses=4] [globalSeconds=300] [perItemMillis=800] [fanout=false]");
    Path input=Paths.get(args[0]),output=Paths.get(args[1]);
    int maxPasses=args.length>2?Integer.parseInt(args[2]):4;
    int globalSeconds=args.length>3?Integer.parseInt(args[3]):300;
    int perItemMillis=args.length>4?Integer.parseInt(args[4]):800;
    boolean fanout=args.length>5&&Boolean.parseBoolean(args[5]);
    String design=input.getFileName().toString();
    BoardHandlingHeadless h=new BoardHandlingHeadless(Locale.US,false,0.01f);
    try(InputStream in=Files.newInputStream(input)) {
      DsnFile.ReadResult result=DsnFile.read(in,h,new BoardObserverAdaptor(),new ItemIdNoGenerator(),TestLevel.RELEASE_VERSION);
      if(result!=DsnFile.ReadResult.OK) throw new IOException("DSN read result "+result);
    }
    RoutingBoard b=h.get_routing_board();
    b.reduce_nets_of_route_items();
    b.change_conduction_is_obstacle(false);
    Settings settings=h.get_settings();
    for(int layer=0;layer<b.get_layer_count();layer++) {
      // Respect the carrier's dedicated reference plane. Native DSN keepout
      // and native KiCad rules independently enforce the same restriction.
      if(b.layer_structure.arr[layer].name.equals("In1.Cu")) settings.autoroute_settings.set_layer_active(layer,false);
    }
    Deadline stop=new Deadline(globalSeconds);
    int initial=incomplete(b),best=initial;
    System.out.printf("LOADED pins=%d traces=%d vias=%d incomplete=%d layers=%d%n",b.get_pins().size(),b.get_traces().size(),b.get_vias().size(),initial,b.get_layer_count());
    checkpoint(b,output,design);
    if(fanout) {
      int attempted=0,routed=0;
      for(Pin pin:new ArrayList<Pin>(b.get_smd_pins())) {
        if(stop.is_stop_requested()) break;
        b.start_marking_changed_area();
        AutorouteEngine.AutorouteResult result=b.fanout(pin,settings,settings.autoroute_settings.get_start_ripup_costs(),stop,new TimeLimit(perItemMillis));
        attempted++;if(result==AutorouteEngine.AutorouteResult.ROUTED) routed++;
      }
      checkpoint(b,output.resolveSibling(output.getFileName().toString()+".fanout.ses"),design);
      System.out.printf("FANOUT attempted=%d routed=%d incomplete=%d%n",attempted,routed,incomplete(b));
    }
    int stagnant=0;
    for(int pass=1;pass<=maxPasses&&!stop.is_stop_requested();pass++) {
      long began=System.nanoTime();int attempted=0,routed=0,failed=0;
      ArrayList<Pin> pins=new ArrayList<Pin>(b.get_pins());
      // Alternate end ordering without modifying the core route algorithm.
      if(pass%2==0) Collections.reverse(pins);
      for(Pin pin:pins) {
        if(stop.is_stop_requested()) break;
        if(pin.net_count()!=1 || pin.get_unconnected_set(pin.get_net_no(0)).isEmpty()) continue;
        b.start_marking_changed_area();
        boolean plane=b.rules.nets.get(pin.get_net_no(0)).contains_plane();
        int viaCosts=plane?settings.autoroute_settings.get_plane_via_costs():settings.autoroute_settings.get_via_costs();
        AutorouteEngine.AutorouteResult result=routeWithRipup(b,pin,settings,viaCosts,pass,stop,perItemMillis);
        attempted++;
        if(result==AutorouteEngine.AutorouteResult.ROUTED) routed++;
        else if(result!=AutorouteEngine.AutorouteResult.ALREADY_CONNECTED) failed++;
      }
      b.finish_autoroute();
      // Official BatchAutorouter pass cleanup, with fanout disabled.
      b.start_marking_changed_area();
      b.remove_trace_tails(-1,fanout?Item.StopConnectionOption.FANOUT_VIA:Item.StopConnectionOption.NONE);
      b.opt_changed_area(new int[0],null,settings.get_trace_pull_tight_accuracy(),
          settings.autoroute_settings.get_trace_cost_arr(),stop,1000);
      int open=incomplete(b);
      Path passPath=output.resolveSibling(output.getFileName().toString()+".pass"+pass+".ses");
      checkpoint(b,passPath,design);
      if(open<=best) { checkpoint(b,output,design);stagnant=open==best?stagnant+1:0;best=open; }
      else stagnant++;
      System.out.printf("PASS %d attempted=%d routed=%d failed=%d incomplete=%d best=%d traces=%d vias=%d seconds=%.3f checkpoint=%s%n",pass,attempted,routed,failed,open,best,b.get_traces().size(),b.get_vias().size(),(System.nanoTime()-began)/1e9,passPath);
      if(open==0 || stagnant>=5) break;
    }
    System.out.printf("DONE initial=%d best_incomplete=%d stopped=%s output=%s%n",initial,best,stop.is_stop_requested(),output);
  }
}
