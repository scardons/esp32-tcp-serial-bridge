#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <time.h>
#include "bridge.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdarg.h>
#include "loss_stats.h"
atomic_long delay_calls=0;
void test_log(char level,const char*tag,const char*format,...){va_list a;va_start(a,format);fprintf(stderr,"%c %s: ",level,tag);vfprintf(stderr,format,a);fprintf(stderr,"\n");va_end(a);}
/* ---- mock UART: TX bytes return on RX after a delay (jumper) */
#define CAP (8*1024*1024)
static uint8_t *ub; static int64_t *ud; static size_t uh,ut; static pthread_mutex_t um=PTHREAD_MUTEX_INITIALIZER;
static atomic_int echo_delay_us=0; static atomic_long uart_tx_total=0; static atomic_int echo_on=1;
static uint8_t *txlog; static atomic_size_t txlen=0;
static atomic_int slow=0; static int64_t m_done=0; static double m_max_backlog_us=0; static long m_writes=0,m_short=0,m_spin=0; static long m_max_ring=0; static pthread_t m_thr; static atomic_int m_thr_set=0;
#define BAUD_US 86.806
#define RING 2048
int uart_driver_write(const uint8_t*d,size_t n){ pthread_mutex_lock(&um); int64_t due=esp_timer_get_time()+echo_delay_us;
  if(!m_thr_set){m_thr=pthread_self();m_thr_set=1;}
  if(slow){ int64_t now=esp_timer_get_time(); double b=m_done>now?(double)(m_done-now):0; long ring_before=(long)(b/BAUD_US); m_writes++; if(n<256)m_short++;
    if(ring_before+(long)n+12>RING)m_spin++; /* driver would spin here */
    m_done=(m_done>now?m_done:now)+(int64_t)(n*BAUD_US+0.5); double nb=(double)(m_done-now); if(nb>m_max_backlog_us)m_max_backlog_us=nb; if(ring_before+(long)n>m_max_ring)m_max_ring=ring_before+n; due=m_done; }
  for(size_t i=0;i<n;i++){ if(txlen<CAP)txlog[txlen++]=d[i]; if(echo_on){ub[ut%CAP]=d[i];ud[ut%CAP]=due;ut++;}} uart_tx_total+=n; pthread_mutex_unlock(&um); return (int)n;}
/* Like the driver, a timed read returns once data arrives, or at the timeout. */
int uart_driver_read(uint8_t*b,size_t cap,uint32_t ms){ size_t n=0; int64_t end=esp_timer_get_time()+ms*1000LL;
  for(;;){ pthread_mutex_lock(&um); int64_t now=esp_timer_get_time();
    while(n<cap&&uh<ut&&ud[uh%CAP]<=now){b[n++]=ub[uh%CAP];uh++;} pthread_mutex_unlock(&um);
    if(n||!ms||esp_timer_get_time()>=end)break; usleep(1000);} return (int)n;}
static void uart_inject_at(const uint8_t*d,size_t n,int64_t due){pthread_mutex_lock(&um);for(size_t i=0;i<n;i++){ub[ut%CAP]=d[i];ud[ut%CAP]=due;ut++;}pthread_mutex_unlock(&um);}
static void uart_inject(const uint8_t*d,size_t n){uart_inject_at(d,n,esp_timer_get_time());}
/* Bytes still in the hardware FIFO when the bridge asks: they reach the ring buffer 1 ms later. */
static atomic_int fifo_at_end=0; static atomic_int fifo_calls=0;
size_t uart_driver_rx_fifo_len(void){ fifo_calls++; int k=atomic_exchange(&fifo_at_end,0);
  if(k>0){uint8_t f[128];memset(f,0xF1,sizeof f);uart_inject_at(f,(size_t)k,esp_timer_get_time()+1000);} return (size_t)(k>0?k:0);}
/* ---- socket wrappers */
atomic_int send_mode=0; atomic_long send_calls=0, send_eagain=0, send_partial=0; atomic_int closes=0; atomic_int last_closed=-1; atomic_int dbl=0;
static atomic_int open_fd=-1;
atomic_long sel_calls=0;
int test_select(int n,fd_set*r,fd_set*w,fd_set*e,struct timeval*t){sel_calls++;return select(n,r,w,e,t);}
ssize_t test_send(int fd,const void*b,size_t n,int fl){ send_calls++; int m=send_mode;
  if(m==3){errno=EWOULDBLOCK;send_eagain++;return -1;}
  if(m==4){errno=ECONNRESET;return -1;}
  if(m==1){int r=rand()%10; if(r<3){errno=EAGAIN;send_eagain++;return -1;} if(r==3){errno=ENOMEM;send_eagain++;return -1;} size_t c=1+rand()%7; if(c<n){n=c;send_partial++;}}
  ssize_t s=send(fd,b,n,fl); return s;}
int test_close(int fd){ if(fd!=open_fd)dbl++; open_fd=-1; closes++; last_closed=fd; return close(fd);}
/* ---- helpers */
static int ls,pass=0,fail=0;
#define CHECK(c,...) do{ if(c){pass++;printf("  ok   ");}else{fail++;printf("  FAIL ");} printf(__VA_ARGS__);printf("\n");fflush(stdout);}while(0)
static void conn(int*cli,int*srv,int rcvbuf){ *cli=socket(AF_INET,SOCK_STREAM,0); if(rcvbuf)setsockopt(*cli,SOL_SOCKET,SO_RCVBUF,&rcvbuf,sizeof rcvbuf);
  struct sockaddr_in a={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};socklen_t l=sizeof a;getsockname(ls,(void*)&a,&l);connect(*cli,(void*)&a,sizeof a);*srv=accept(ls,0,0);}
static esp_err_t attach(int fd){ esp_err_t e=bridge_attach_client(fd); if(e==ESP_OK)open_fd=fd; return e;}
static void wait_closed(int want,int ms){while(ms-->0&&closes<want)usleep(1000);}
static void wait_free(void){usleep(30000);}
static size_t rd_all(int fd,uint8_t*b,size_t want,int ms){size_t g=0;struct timeval tv={0,50000};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);int64_t end=esp_timer_get_time()+ms*1000LL;while(g<want&&esp_timer_get_time()<end){ssize_t r=recv(fd,b+g,want-g,0);if(r>0)g+=r;else if(r==0)break;}return g;}
/* Reads until the stream ends: 0 for EOF (a FIN), the errno for a reset or error, -1 if it does not end in time. */
static int stream_end(int fd,int ms){uint8_t d[4096];struct timeval tv={0,50000};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);int64_t end=esp_timer_get_time()+ms*1000LL;while(esp_timer_get_time()<end){ssize_t r=recv(fd,d,sizeof d,0);if(r==0)return 0;if(r<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)return errno;}return -1;}
struct wr{int fd;const uint8_t*b;size_t n;};
static void*wth(void*p){struct wr*w=p;size_t o=0;while(o<w->n){size_t c=w->n-o;if(c>4096)c=4096;ssize_t s=write(w->fd,w->b+o,c);if(s<=0)break;o+=s;}return 0;}
static void *pat(size_t n){uint8_t*b=malloc(n);for(size_t i=0;i<n;i++)b[i]=(uint8_t)((i*7)^(i>>8));return b;}
static int run_echo(const char*name,int mode,size_t n,int rcvbuf){ uint8_t*tx=pat(n),*rx=malloc(n);int c,s;conn(&c,&s,rcvbuf);send_mode=mode;send_partial=0;send_eagain=0;
  int bc=closes; CHECK(attach(s)==ESP_OK,"%s: attach",name); pthread_t w;struct wr wa={c,tx,n};pthread_create(&w,0,wth,&wa);
  size_t g=rd_all(c,rx,n,120000);pthread_join(w,0);send_mode=0;
  CHECK(g==n,"%s: received %zu of %zu",name,g,n); CHECK(g==n&&!memcmp(tx,rx,n),"%s: bytes identical (order and values)",name);
  printf("       partial sends %ld, temporary failures %ld\n",(long)send_partial,(long)send_eagain);
  close(c);wait_closed(bc+1,2000);wait_free();free(tx);free(rx);return 0;}
int main(void){ ub=malloc(CAP);ud=malloc(CAP*sizeof(int64_t));txlog=malloc(CAP);setvbuf(stdout,0,_IOLBF,0);
  ls=socket(AF_INET,SOCK_STREAM,0);struct sockaddr_in a={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};bind(ls,(void*)&a,sizeof a);listen(ls,8);
  CHECK(bridge_attach_client(99)==ESP_ERR_INVALID_STATE,"attach before start refused");
  CHECK(bridge_start()==ESP_OK,"bridge_start"); CHECK(bridge_start()==ESP_ERR_INVALID_STATE,"second start refused");
  printf("T1 all 256 byte values, FR-3 and FR-4 echo\n");
  { uint8_t all[256];for(int i=0;i<256;i++)all[i]=i;uint8_t tx[2560],rx[2560];for(int i=0;i<2560;i++)tx[i]=all[i%256];int c,s;conn(&c,&s,0);int b=closes;txlen=0;attach(s);usleep(250000);
    send(c,tx,sizeof tx,0);size_t g=rd_all(c,rx,sizeof rx,5000);CHECK(g==sizeof rx&&!memcmp(tx,rx,g),"2560 bytes with every value incl 0x00 echoed intact (%zu)",g);
    CHECK(txlen==sizeof tx&&!memcmp(txlog,tx,sizeof tx),"UART TX saw identical bytes (FR-3)");close(c);wait_closed(b+1,2000);wait_free();}
  printf("T2 partial and failing sends\n"); run_echo("fragmenting send",1,2048,0);
  printf("T3 large normal transfer\n"); run_echo("normal send",0,512*1024,0);
  printf("T4 second client rejected, slot released, no socket leak\n");
  { int c1,s1,c2,s2;conn(&c1,&s1,0);conn(&c2,&s2,0);int b=closes;CHECK(attach(s1)==ESP_OK,"client A attached");CHECK(bridge_attach_client(s2)==ESP_ERR_INVALID_STATE,"client B refused while A active");close(s2);close(c2);
    close(c1);wait_closed(b+1,2000);wait_free();conn(&c2,&s2,0);CHECK(attach(s2)==ESP_OK,"client B accepted after A left");close(c2);wait_closed(b+2,2000);wait_free();CHECK(dbl==0,"no double close");}
  printf("T5 20 connect/disconnect cycles\n");
  { int b=closes,ok=1;for(int i=0;i<20;i++){int c,s;conn(&c,&s,0);if(attach(s)!=ESP_OK)ok=0;close(c);wait_closed(b+i+1,2000);wait_free();}CHECK(ok&&closes==b+20,"20 cycles, %d closes",closes-b);CHECK(dbl==0,"no double close");}
  printf("T6 stale data never reaches a new client\n");
  { echo_on=1;uint8_t st[900];memset(st,0xAB,sizeof st);uart_inject(st,sizeof st);int c,s;conn(&c,&s,0);int b=closes;attach(s);uint8_t r[64];size_t g=rd_all(c,r,sizeof r,500);CHECK(g==0,"data received with no client is discarded (%zu)",g);close(c);wait_closed(b+1,2000);wait_free();
    // bytes still leaving TX when session 1 ends must not reach session 2
    echo_delay_us=150000;conn(&c,&s,0);b=closes;attach(s);usleep(250000);uint8_t old[1024];memset(old,0xCD,sizeof old);send(c,old,sizeof old,0);usleep(40000);close(c);wait_closed(b+1,2000);
    int c2,s2;conn(&c2,&s2,0);attach(s2);uint8_t r2[64];g=rd_all(c2,r2,sizeof r2,700);CHECK(g==0,"bytes echoed after session end not delivered to next client (%zu)",g);
    uint8_t nw[32];memset(nw,0x5A,sizeof nw);uart_inject(nw,sizeof nw);g=rd_all(c2,r2,32,1000);CHECK(g==32&&r2[0]==0x5A,"new session still receives fresh data (%zu)",g);echo_delay_us=0;b=closes;close(c2);wait_closed(b+1,2000);wait_free();}
  printf("T7 disconnect request\n");
  { int c,s;conn(&c,&s,0);int b=closes;bridge_request_disconnect();wait_free();CHECK(attach(s)==ESP_OK,"request before attach ignored by new client");usleep(100000);CHECK(closes==b,"new client stays connected");bridge_request_disconnect();bridge_request_disconnect();wait_closed(b+1,2000);CHECK(closes==b+1,"session closed once");wait_free();int e=stream_end(c,2000);CHECK(e==0,"requested disconnect ends with a FIN, not a reset (%s)",e==0?"EOF":e<0?"no end":strerror(e));close(c);conn(&c,&s,0);CHECK(attach(s)==ESP_OK,"accepting again");b=closes;close(c);wait_closed(b+1,2000);wait_free();CHECK(dbl==0,"no double close");}
  printf("T7b healthy peer close\n");
  { echo_on=1;int c,s;conn(&c,&s,0);int b=closes;attach(s);usleep(250000);uint8_t m[32];memset(m,0x33,sizeof m);send(c,m,sizeof m,0);uint8_t r[32];CHECK(rd_all(c,r,sizeof r,1000)==sizeof r,"echo before close");shutdown(c,SHUT_WR);wait_closed(b+1,2000);CHECK(closes==b+1,"client FIN ends the session");int e=stream_end(c,2000);CHECK(e==0,"server answers with a FIN, not a reset (%s)",e==0?"EOF":e<0?"no end":strerror(e));close(c);wait_free();CHECK(dbl==0,"no double close");}
  printf("T8 stall: send never progresses, EAGAIN must not end session early\n");
  { echo_on=0;int c,s;conn(&c,&s,0);int b=closes;send_mode=3;attach(s);usleep(250000);uint8_t x[64]={1};uart_inject(x,sizeof x);long c0=send_calls;int64_t t0=esp_timer_get_time();
    usleep(4000000);CHECK(closes==b,"still connected after 4 s of EAGAIN");wait_closed(b+1,3000);double el=(esp_timer_get_time()-t0)/1e6;long calls=send_calls-c0;CHECK(closes==b+1&&el>4.9&&el<5.3,"closed at %.2f s",el);CHECK(calls>100&&calls<=520,"%ld send attempts in stall, about 100/s, not a busy loop",calls);send_mode=0;close(c);wait_free();}
  printf("T9 real stalled client (not reading), full socket buffers\n");
  { echo_on=0;int c,s;conn(&c,&s,4096);int sb=4096;setsockopt(s,SOL_SOCKET,SO_SNDBUF,&sb,sizeof sb);int b=closes;attach(s);usleep(250000);static uint8_t big[1024*1024];int64_t t0=0;uart_inject(big,sizeof big);t0=esp_timer_get_time();wait_closed(b+1,15000);CHECK(closes==b+1,"client that never reads is disconnected after %.2f s",(esp_timer_get_time()-t0)/1e6);int e=stream_end(c,5000);CHECK(e==ECONNRESET,"stalled client sees ECONNRESET, not a FIN (%s)",e==0?"EOF":e<0?"no end":strerror(e));close(c);wait_free();uint8_t d[4096];while(uart_driver_read(d,sizeof d,0)>0){}}
  printf("T10 progress resets the timer\n");
  { echo_on=0;int c,s;conn(&c,&s,4096);int sb=4096;setsockopt(s,SOL_SOCKET,SO_SNDBUF,&sb,sizeof sb);int b=closes;attach(s);usleep(250000);static uint8_t big2[1024*1024];uart_inject(big2,sizeof big2);uint8_t d[8192];int64_t t0=esp_timer_get_time();
    while(esp_timer_get_time()-t0<11000000){usleep(3000000);recv(c,d,sizeof d,MSG_DONTWAIT);}CHECK(closes==b,"slow reader survives 11 s, reading every 3 s");close(c);wait_closed(b+1,8000);CHECK(closes==b+1,"then closes");wait_free();}
  printf("T11 peer reset\n");
  { echo_on=1;int c,s;conn(&c,&s,0);int b=closes;attach(s);usleep(250000);struct linger l={1,0};setsockopt(c,SOL_SOCKET,SO_LINGER,&l,sizeof l);close(c);wait_closed(b+1,2000);CHECK(closes==b+1,"RST ends session");CHECK(dbl==0,"no double close");}
  printf("T12 slow UART transmitter, client much faster than the baud rate\n");
  { echo_on=1; slow=1; m_done=0; m_max_backlog_us=0; m_writes=m_short=m_spin=m_max_ring=0; usleep(250000);
    size_t n=64*1024; uint8_t*tx=pat(n),*rx=malloc(n); int c,s; conn(&c,&s,0); int b=closes; txlen=0; attach(s); usleep(250000);
    clockid_t cid; pthread_getcpuclockid(pthread_self(),&cid); (void)cid;
    long d0=delay_calls,s0=sel_calls,sc0=send_calls; struct timespec c0,c1; int64_t t0=esp_timer_get_time(); clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&c0);
    pthread_t w; struct wr wa={c,tx,n}; pthread_create(&w,0,wth,&wa); size_t g=rd_all(c,rx,n,20000); pthread_join(w,0);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&c1); double el=(esp_timer_get_time()-t0)/1e6, cpu=(c1.tv_sec-c0.tv_sec)+(c1.tv_nsec-c0.tv_nsec)/1e9;
    CHECK(g==n,"received %zu of %zu",g,n); CHECK(g==n&&!memcmp(tx,rx,n),"64 KB echoed in order, no loss or duplication");
    CHECK(txlen==n&&!memcmp(txlog,tx,n),"UART TX saw every byte once, in order (%zu)",(size_t)txlen);
    CHECK(m_max_backlog_us<=100000.0,"max TX backlog %.1f ms, limit 100 ms",m_max_backlog_us/1000);
    CHECK(m_max_ring<=1400,"max ring occupancy %ld of %d bytes",m_max_ring,RING);
    CHECK(m_spin==0,"driver would have spun on %ld of %ld writes",m_spin,m_writes);
    CHECK(m_short>0,"blocks split by the budget and resumed: %ld short writes of %ld",m_short,m_writes);
    double rate=n/el; CHECK(rate>9000&&rate<=11600,"throughput %.0f bytes/s in %.2f s (baud limit 11520)",rate,el);
    long iters=(delay_calls-d0)+(sel_calls-s0); CHECK(iters/el<400,"%.0f loop passes/s, %ld sleeps and %ld selects, %ld sends",iters/el,delay_calls-d0,sel_calls-s0,send_calls-sc0);
    CHECK(cpu/el<0.25,"process CPU %.1f%% of one core during transfer",100*cpu/el);
    close(c); wait_closed(b+1,3000); wait_free(); free(tx); free(rx); }
  printf("T13 UART to TCP stays served while TCP to UART is throttled\n");
  { echo_on=0; slow=1; m_done=0; usleep(250000); size_t n=20000; uint8_t*tx=pat(n); int c,s; conn(&c,&s,0); int b=closes; txlen=0; attach(s); usleep(250000);
    pthread_t w; struct wr wa={c,tx,n}; pthread_create(&w,0,wth,&wa); usleep(300000);
    int ok=1; double worst=0; for(int i=0;i<30;i++){ uint8_t m[16]; memset(m,0x40+i,16); int64_t t=esp_timer_get_time(); uart_inject(m,16); uint8_t r[16]; size_t g=rd_all(c,r,16,1000); double l=(esp_timer_get_time()-t)/1e3; if(l>worst)worst=l; if(g!=16||memcmp(m,r,16))ok=0; usleep(20000);}
    CHECK(ok,"30 UART messages delivered during throttling"); CHECK(worst<100,"worst UART to TCP latency %.1f ms",worst); pthread_join(w,0); usleep(100000);
    close(c); wait_closed(b+1,4000); wait_free(); CHECK(txlen==n&&!memcmp(txlog,tx,n),"throttled TX complete after peer FIN (%zu of %zu)",(size_t)txlen,n); free(tx);}
  printf("T14 client sends a block and closes at once: no byte lost\n");
  { echo_on=0; slow=1; m_done=0; usleep(250000); size_t n=3000; uint8_t*tx=pat(n); int c,s; conn(&c,&s,0); int b=closes; txlen=0; attach(s); write(c,tx,n); close(c); wait_closed(b+1,3000);
    CHECK(closes==b+1&&txlen==n&&!memcmp(txlog,tx,n),"all %zu bytes reached UART before close",(size_t)txlen); wait_free(); free(tx);}
  printf("T15 disconnect request while throttled with bytes pending\n");
  { echo_on=0; slow=1; m_done=0; usleep(250000); size_t n=20000; uint8_t*tx=pat(n); int c,s; conn(&c,&s,0); int b=closes; txlen=0; attach(s); pthread_t w; struct wr wa={c,tx,n}; pthread_create(&w,0,wth,&wa); usleep(500000); int64_t t=esp_timer_get_time(); bridge_request_disconnect(); wait_closed(b+1,1000); double ms=(esp_timer_get_time()-t)/1e3; CHECK(closes==b+1&&ms<60,"closed in %.1f ms",ms); CHECK(dbl==0,"no double close"); close(c); pthread_join(w,0); wait_free(); free(tx); slow=0;}
  printf("T16 FR-7 accounting at session end\n");
  { static loss_snapshot_t p,c,d; echo_on=0; slow=0;
#define SNAP() loss_stats_snapshot(&p)
#define DELTA() do{loss_stats_snapshot(&c);loss_stats_delta(&p,&c,&d);}while(0)
    usleep(300000); SNAP(); uint8_t x[300]; memset(x,1,sizeof x);
    uart_inject(x,sizeof x); usleep(300000); DELTA();
    CHECK(d.values[DISCARD_NO_CLIENT_BYTES]==300,"no client: 300 bytes counted as expected discard (%u)",d.values[DISCARD_NO_CLIENT_BYTES]);
    CHECK(!loss_stats_has_loss(&d),"no client: not reported as data loss");
    { int c1,s1;conn(&c1,&s1,0);int b=closes;SNAP();attach(s1);usleep(250000);write(c1,"hello",5);usleep(100000);close(c1);wait_closed(b+1,2000);wait_free();DELTA();
      CHECK(!loss_stats_has_loss(&d)&&d.values[DISCARD_SESSION_BYTES]==0,"clean session: every counter stays at zero"); }
    { int c1,s1;conn(&c1,&s1,0);int b=closes;send_mode=3;attach(s1);usleep(250000);SNAP();uart_inject(x,100);usleep(100000);close(c1);wait_closed(b+1,2000);send_mode=0;wait_free();DELTA();
      CHECK(d.values[DISCARD_SESSION_BYTES]==100,"normal close with pending bytes: expected discard (%u)",d.values[DISCARD_SESSION_BYTES]);
      CHECK(!loss_stats_has_loss(&d),"normal close: not reported as data loss"); }
    { int c1,s1;conn(&c1,&s1,0);int b=closes;attach(s1);usleep(250000);SNAP();send_mode=4;uart_inject(x,120);wait_closed(b+1,2000);send_mode=0;wait_free();DELTA();close(c1);
      CHECK(d.values[LOSS_TCP_SEND_FAILURE]==1&&d.values[LOSS_TCP_TX_BYTES]==120,"send error: 1 failure and 120 bytes lost (%u, %u)",d.values[LOSS_TCP_SEND_FAILURE],d.values[LOSS_TCP_TX_BYTES]);
      CHECK(d.values[DISCARD_SESSION_BYTES]==0,"send error: not counted as expected discard"); }
    { int c1,s1;conn(&c1,&s1,0);int b=closes;send_mode=3;attach(s1);usleep(250000);SNAP();uart_inject(x,200);wait_closed(b+1,7000);send_mode=0;wait_free();DELTA();close(c1);
      CHECK(d.values[LOSS_TCP_SEND_FAILURE]==1&&d.values[LOSS_TCP_TX_BYTES]==200,"stalled client: 1 failure and 200 bytes lost (%u, %u)",d.values[LOSS_TCP_SEND_FAILURE],d.values[LOSS_TCP_TX_BYTES]); }
    { int c1,s1;conn(&c1,&s1,0);int b=closes;attach(s1);usleep(250000);SNAP();send_mode=3;uart_inject(x,64);usleep(100000);bridge_request_disconnect();wait_closed(b+1,1000);send_mode=0;wait_free();DELTA();close(c1);
      CHECK(d.values[LOSS_TCP_TX_BYTES]==64&&d.values[LOSS_TCP_SEND_FAILURE]==0,"disconnect request: 64 bytes lost, no send failure (%u)",d.values[LOSS_TCP_TX_BYTES]); }
    { echo_on=0;slow=1;m_done=0;usleep(250000);size_t n=20000;uint8_t*tx=pat(n);int c1,s1;conn(&c1,&s1,0);int b=closes;txlen=0;attach(s1);pthread_t w;struct wr wa={c1,tx,n};pthread_create(&w,0,wth,&wa);usleep(600000);SNAP();bridge_request_disconnect();wait_closed(b+1,1000);DELTA();close(c1);pthread_join(w,0);wait_free();free(tx);slow=0;
      printf("       TCP to UART bytes left unwritten at close: %u\n",d.values[LOSS_UART_TX_BYTES]);CHECK(d.values[LOSS_UART_TX_BYTES]<=256,"unwritten TCP bytes bounded by the 256-byte pending buffer");CHECK(dbl==0,"no double close"); } }
  printf("T17 M3: bytes UART1 held when a session ended are counted once, with that session\n");
  { static loss_snapshot_t p,c,d; echo_on=0; slow=0; uint8_t x[1000]; memset(x,7,sizeof x);
    const char*names[]={"H6 stalled client","Ethernet link loss (disconnect request)","abnormal disconnect (RST)","normal disconnect (FIN)"};
    for(int k=0;k<4;k++){ int c1,s1;conn(&c1,&s1,0);int b=closes;attach(s1);usleep(250000);loss_stats_snapshot(&p);int f0=fifo_calls;
      send_mode=3; uart_inject(x,sizeof x); usleep(100000); fifo_at_end=40;
      if(k==1)bridge_request_disconnect();
      if(k==2){struct linger l={1,0};setsockopt(c1,SOL_SOCKET,SO_LINGER,&l,sizeof l);close(c1);}
      if(k==3)close(c1);
      wait_closed(b+1,k==0?7000:2000); send_mode=0; wait_free(); usleep(300000);
      loss_stats_snapshot(&c);loss_stats_delta(&p,&c,&d); if(k<2)close(c1);
      uint32_t tx=d.values[LOSS_TCP_TX_BYTES],ses=d.values[DISCARD_SESSION_BYTES],nc=d.values[DISCARD_NO_CLIENT_BYTES];
      printf("       %s: tcp_tx_bytes=%u session_bytes=%u no_client_bytes=%u tcp_send_failures=%u\n",names[k],tx,ses,nc,d.values[LOSS_TCP_SEND_FAILURE]);
      CHECK(closes==b+1&&fifo_calls>f0,"%s: session ended and the FIFO was checked",names[k]);
      CHECK(tx+ses+nc==1040,"%s: 1000 ring and pending bytes plus 40 FIFO bytes, each counted once (%u)",names[k],tx+ses+nc);
      CHECK(nc==0,"%s: none counted as no-client data",names[k]);
      if(k<3) CHECK(tx==1040&&ses==0&&loss_stats_has_loss(&d),"%s: all 1040 counted as data loss",names[k]);
      else CHECK(ses==1040&&tx==0&&!loss_stats_has_loss(&d),"%s: all 1040 counted as expected session discard",names[k]);
      if(k==0) CHECK(d.values[LOSS_TCP_SEND_FAILURE]==1,"%s: one send failure",names[k]);
      if(k==1) CHECK(d.values[LOSS_TCP_SEND_FAILURE]==0,"%s: no send failure",names[k]); }
    loss_stats_snapshot(&p); uart_inject(x,300); usleep(300000); loss_stats_snapshot(&c);loss_stats_delta(&p,&c,&d);
    CHECK(d.values[DISCARD_NO_CLIENT_BYTES]==300&&!loss_stats_has_loss(&d)&&d.values[DISCARD_SESSION_BYTES]==0,"afterwards, data with no client is still an expected discard (%u)",d.values[DISCARD_NO_CLIENT_BYTES]);
    { uint8_t big[4000]; memset(big,9,sizeof big); int c1,s1;conn(&c1,&s1,0);int b=closes;attach(s1);usleep(250000);loss_stats_snapshot(&p);
      send_mode=3; uart_inject(big,sizeof big); usleep(100000); bridge_request_disconnect(); wait_closed(b+1,2000); send_mode=0; wait_free(); usleep(300000);
      loss_stats_snapshot(&c);loss_stats_delta(&p,&c,&d); close(c1);
      CHECK(d.values[LOSS_TCP_TX_BYTES]>=2048&&d.values[LOSS_TCP_TX_BYTES]<=256+2304,"drain stops at what UART1 can hold (%u bytes counted as loss)",d.values[LOSS_TCP_TX_BYTES]);
      CHECK(d.values[LOSS_TCP_TX_BYTES]+d.values[DISCARD_NO_CLIENT_BYTES]==4000,"bytes beyond it go to no-client data, none counted twice (%u)",d.values[LOSS_TCP_TX_BYTES]+d.values[DISCARD_NO_CLIENT_BYTES]); } }
  printf("\nRESULT pass=%d fail=%d send_calls=%ld\n",pass,fail,(long)send_calls);return fail?1:0;}
