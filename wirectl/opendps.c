#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "opendps.h"
#define SOF 0x7e
#define DLE 0x7d
#define EOF_ 0x7f
static void (*idle_hook)(void);
void opendps_set_idle_hook(void (*hook)(void)){idle_hook=hook;}
static uint16_t crc_add(uint16_t c,uint8_t b){uint8_t x=(uint8_t)(c>>8)^b;x^=x>>4;return(uint16_t)((c<<8)^((uint16_t)x<<12)^((uint16_t)x<<5)^x);}
static size_t frame(const uint8_t*p,size_t n,uint8_t*o){uint16_t c=0;size_t w=0;o[w++]=SOF;for(size_t i=0;i<n+2;i++){uint8_t b;if(i<n){b=p[i];c=crc_add(c,b);}else b=i==n?(uint8_t)(c>>8):(uint8_t)c;if(b==SOF||b==DLE||b==EOF_){o[w++]=DLE;o[w++]=b^0x20;}else o[w++]=b;}o[w++]=EOF_;return w;}
static bool txrx(opendps_t*d,const uint8_t*p,size_t n,uint8_t*r,size_t*rn,uint32_t ms){uint8_t f[96];size_t fl=frame(p,n,f),w=0;while(uart_is_readable(d->uart))(void)uart_getc(d->uart);uart_write_blocking(d->uart,f,fl);absolute_time_t end=make_timeout_time_ms(ms);bool started=false,esc=false,ended=false;while(!time_reached(end)){if(!uart_is_readable(d->uart)){if(idle_hook)idle_hook();else tight_loop_contents();continue;}uint8_t b=(uint8_t)uart_getc(d->uart);if(b==SOF){started=true;esc=false;w=0;continue;}if(!started)continue;if(b==EOF_){ended=true;break;}if(esc){b^=0x20;esc=false;}else if(b==DLE){esc=true;continue;}if(w<sizeof(f))f[w++]=b;}if(!ended||w<3)return false;uint16_t c=0;for(size_t i=0;i+2<w;i++)c=crc_add(c,f[i]);if(c!=(uint16_t)((f[w-2]<<8)|f[w-1]))return false;w-=2;if(w>*rn)return false;memcpy(r,f,w);*rn=w;return true;}
void opendps_init(opendps_t*d,uart_inst_t*u,uint tx,uint rx){memset(d,0,sizeof(*d));d->uart=u;uart_init(u,9600);uart_set_format(u,8,1,UART_PARITY_NONE);gpio_set_function(tx,GPIO_FUNC_UART);gpio_set_function(rx,GPIO_FUNC_UART);}
static uint16_t be16(const uint8_t*p){return(uint16_t)((p[0]<<8)|p[1]);}
bool opendps_query(opendps_t*d,uint32_t ms){uint8_t q=4,r[94];size_t n=sizeof(r);if(!txrx(d,&q,1,r,&n,ms)||n<16||r[0]!=0x84||!r[1])return false;d->voltage=be16(r+4);d->current=be16(r+6);d->output_enabled=r[8]!=0;size_t x=15;while(x<n&&r[x++]){}while(x<n){char*k=(char*)r+x;size_t kl=strnlen(k,n-x);if(kl==n-x)break;x+=kl+1;if(x>=n)break;char*v=(char*)r+x;size_t vl=strnlen(v,n-x);if(vl==n-x)break;x+=vl+1;unsigned long z=strtoul(v,NULL,10);if(!strcmp(k,"voltage"))d->voltage_set=(uint16_t)z;else if(!strcmp(k,"current"))d->current_set=(uint16_t)z;}return true;}
static bool setp(opendps_t*d,const char*k,uint16_t v){uint8_t p[32]={14},r[8];char s[8];size_t n=1,rn=sizeof(r),l=strlen(k);memcpy(p+n,k,l+1);n+=l+1;snprintf(s,sizeof(s),"%u",v);l=strlen(s);memcpy(p+n,s,l+1);n+=l+1;return txrx(d,p,n,r,&rn,120)&&rn>=3&&r[0]==0x8e&&r[1]&&r[2]==0;}
bool opendps_set_voltage(opendps_t*d,uint16_t v){if(!setp(d,"voltage",v))return false;d->voltage_set=v;return true;}
bool opendps_set_current(opendps_t*d,uint16_t v){if(!setp(d,"current",v))return false;d->current_set=v;return true;}
bool opendps_set_output(opendps_t*d,bool on){uint8_t p[2]={12,on},r[8];size_t n=sizeof(r);if(!txrx(d,p,2,r,&n,120)||n<2||r[0]!=0x8c||!r[1])return false;d->output_enabled=on;return true;}
bool opendps_is_cc(const opendps_t*d){int ve=(int)d->voltage_set-d->voltage,ie=(int)d->current_set-d->current;return d->output_enabled&&(ve<0?-ve:ve)>(ie<0?-ie:ie);}
