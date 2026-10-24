#define main upstream_test_main
#include "/workspace/scratch/45de2288a575/K-UI-recovery/tests/test_retail_image.c"
#undef main
int main(void) {
 for(unsigned cooked=0;cooked<2;cooked++) for(unsigned chunk=1;chunk<=8;chunk*=2) {
  fixture_format(false,cooked?13u:0u); image.read_run=read_run;
  for(unsigned done=0;done<32;done+=chunk)
   CHECK(kui_retail_image_read(&image,45000+done,chunk,KUI_GAME_SECTOR_MODE1,output,sizeof(output))==KUI_GAME_OK);
  printf("%s chunk=%u: blocks=%u run_callbacks=%u user_bytes=%u\n",cooked?"cooked2048":"raw2352",chunk,calls,run_calls,32u*2048u);
 }
 return 0;
}
