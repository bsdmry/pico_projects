#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "led14seg.h"
#include "opendps.h"
#include "wirectl.h"

typedef enum { OUT_PSU_CONN_FAULT,OUT_OFF,OUT_OFF_V_STEP_CHG,OUT_OFF_I_STEP_CHG,
               OUT_ON,OUT_ON_V_STEP_CHG,OUT_ON_I_STEP_CHG } channel_mode_t;
typedef struct { channel_mode_t mode; uint16_t vstep,istep; bool connected; } channel_t;
typedef struct { uint a,b,sw; uint8_t old; int8_t acc; volatile int8_t turns; volatile bool press; } encoder_t;
static volatile scpi_registers_t regs;
static led14seg_display_t seg[2]; static opendps_t psu[2]; static channel_t ch[2];
static encoder_t enc[4]={{WIRECTL_ENC_V1_A,WIRECTL_ENC_V1_B,WIRECTL_ENC_V1_SW},
 {WIRECTL_ENC_V2_A,WIRECTL_ENC_V2_B,WIRECTL_ENC_V2_SW},{WIRECTL_ENC_I1_A,WIRECTL_ENC_I1_B,WIRECTL_ENC_I1_SW},
 {WIRECTL_ENC_I2_A,WIRECTL_ENC_I2_B,WIRECTL_ENC_I2_SW}};
static volatile bool power_press[2],wifi_press; static enum wifi_led_state wifi_state;
static absolute_time_t status_until;
static bool status_active;
static uint16_t led_bits=0xffff; static const int8_t qdelta[16]={0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
static volatile uint32_t q0[32],q1[32]; static volatile uint8_t r0,w0,r1,w1;

static void fifo_irq1(void){while(multicore_fifo_rvalid()){uint8_t n=(w1+1)&31;uint32_t m=multicore_fifo_pop_blocking();if(n!=r1){q1[w1]=m;w1=n;}}multicore_fifo_clear_irq();}
static void fifo_irq0(void){while(multicore_fifo_rvalid()){uint8_t n=(w0+1)&31;uint32_t m=multicore_fifo_pop_blocking();if(n!=r0){q0[w0]=m;w0=n;}}multicore_fifo_clear_irq();}
void intercore_send(enum intercore_command c,uint32_t v){multicore_fifo_push_blocking(CORE_MESSAGE(c,v));}
bool intercore_core0_try_pop(uint32_t*m){if(r0==w0)return false;*m=q0[r0];r0=(r0+1)&31;return true;}
static void reply(enum intercore_command c,uint32_t v){multicore_fifo_push_blocking(CORE_MESSAGE(c,v));}

static void led_set(unsigned bit,bool on){if(on)led_bits&=(uint16_t)~(1u<<bit);else led_bits|=(uint16_t)(1u<<bit);}
static void leds_write(void){uint8_t b[2]={(uint8_t)(led_bits>>8),(uint8_t)led_bits};gpio_put(WIRECTL_LED_CS,0);spi_write_blocking(spi0,b,2);gpio_put(WIRECTL_LED_CS,1);}
static void service_displays(void){redraw_display(&seg[0]);redraw_display(&seg[1]);}
static void update_leds(void){led_set(13,regs.locked[0]);led_set(12,regs.remote[0]);led_set(11,regs.current_lim_trg[0]);led_set(10,regs.output_en[0]);led_set(9,!regs.output_en[0]&&ch[0].connected);led_set(7,!regs.output_en[1]&&ch[1].connected);led_set(3,regs.locked[1]);led_set(2,regs.remote[1]);led_set(1,regs.current_lim_trg[1]);led_set(0,regs.output_en[1]);led_set(6,wifi_state==WIFI_LED_ERROR);led_set(5,wifi_state==WIFI_LED_OK);led_set(4,wifi_state==WIFI_LED_CONNECTING);leds_write();}

static void input(uint p){gpio_init(p);gpio_set_dir(p,GPIO_IN);gpio_pull_up(p);}
static void gpio_irq(uint gpio,uint32_t events){(void)events;
 for(unsigned i=0;i<4;i++){if(gpio==enc[i].sw){enc[i].press=true;return;}if(gpio==enc[i].a||gpio==enc[i].b){uint8_t now=(gpio_get(enc[i].a)?2:0)|(gpio_get(enc[i].b)?1:0);enc[i].acc+=qdelta[(enc[i].old<<2)|now];enc[i].old=now;if(enc[i].acc>=4){enc[i].turns++;enc[i].acc=0;}else if(enc[i].acc<=-4){enc[i].turns--;enc[i].acc=0;}return;}}
 if(gpio==WIRECTL_BTN_PWR1)power_press[0]=true;else if(gpio==WIRECTL_BTN_PWR2)power_press[1]=true;else if(gpio==WIRECTL_BTN_WIFI)wifi_press=true;
}
static void controls_init(void){for(unsigned i=0;i<4;i++){input(enc[i].a);input(enc[i].b);input(enc[i].sw);enc[i].old=(gpio_get(enc[i].a)?2:0)|(gpio_get(enc[i].b)?1:0);}input(WIRECTL_BTN_PWR1);input(WIRECTL_BTN_PWR2);input(WIRECTL_BTN_WIFI);
 gpio_set_irq_enabled_with_callback(enc[0].a,GPIO_IRQ_EDGE_RISE|GPIO_IRQ_EDGE_FALL,true,gpio_irq);
 for(unsigned i=0;i<4;i++){gpio_set_irq_enabled(enc[i].a,GPIO_IRQ_EDGE_RISE|GPIO_IRQ_EDGE_FALL,true);gpio_set_irq_enabled(enc[i].b,GPIO_IRQ_EDGE_RISE|GPIO_IRQ_EDGE_FALL,true);gpio_set_irq_enabled(enc[i].sw,GPIO_IRQ_EDGE_FALL,true);}gpio_set_irq_enabled(WIRECTL_BTN_PWR1,GPIO_IRQ_EDGE_FALL,true);gpio_set_irq_enabled(WIRECTL_BTN_PWR2,GPIO_IRQ_EDGE_FALL,true);gpio_set_irq_enabled(WIRECTL_BTN_WIFI,GPIO_IRQ_EDGE_FALL,true);}

static void field_value(unsigned c,bool voltage,uint16_t v){uint8_t a=c?8:0,b=c?15:7;if(voltage){write_float(&seg[0],v/1000.0f,a,a+4,2,3,0,0,1);write_string(&seg[0]," V",2,a+5,b);}else{write_float(&seg[1],v/1000.0f,a,a+4,1,3,0,0,1);write_string(&seg[1]," A",2,a+5,b);}}
static void render(unsigned c){uint8_t a=c?8:0,b=c?15:7;blink_segment(&seg[0],false,(uint16_t)(0xffu<<a));blink_segment(&seg[1],false,(uint16_t)(0xffu<<a));if(!ch[c].connected){write_string(&seg[0],"pwr_conn",8,a,b);write_string(&seg[1]," fault! ",8,a,b);return;}bool on=ch[c].mode>=OUT_ON;field_value(c,true,on?regs.voltage[c]:regs.voltage_lim[c]);field_value(c,false,on?regs.current[c]:regs.current_lim[c]);if(ch[c].mode==OUT_OFF_V_STEP_CHG||ch[c].mode==OUT_ON_V_STEP_CHG){unsigned pos=a+(ch[c].vstep==1000?1:ch[c].vstep==100?2:3);blink_segment(&seg[0],true,(uint16_t)(1u<<pos));}if(ch[c].mode==OUT_OFF_I_STEP_CHG||ch[c].mode==OUT_ON_I_STEP_CHG){unsigned pos=a+(ch[c].istep==100?1:2);blink_segment(&seg[1],true,(uint16_t)(1u<<pos));}}
static bool base_on(channel_mode_t m){return m==OUT_ON||m==OUT_ON_V_STEP_CHG||m==OUT_ON_I_STEP_CHG;}
static void apply_turn(unsigned ei,int d){unsigned c=ei&1u;bool volt=ei<2;if(!ch[c].connected||regs.locked[c])return;channel_mode_t m=ch[c].mode;if((m==OUT_OFF_V_STEP_CHG||m==OUT_ON_V_STEP_CHG)&&volt){static const uint16_t s[]={10,100,1000};int x=ch[c].vstep==10?0:ch[c].vstep==100?1:2;x+=d;x=x<0?2:x>2?0:x;ch[c].vstep=s[x];render(c);return;}if((m==OUT_OFF_I_STEP_CHG||m==OUT_ON_I_STEP_CHG)&&!volt){ch[c].istep=ch[c].istep==10?100:10;render(c);return;}if(m!=OUT_OFF&&m!=OUT_ON)return;uint16_t *v=volt?(uint16_t*)&regs.voltage_lim[c]:(uint16_t*)&regs.current_lim[c];uint16_t step=volt?ch[c].vstep:ch[c].istep,max=volt?WIRECTL_VOLTAGE_MAX:WIRECTL_CURRENT_MAX;int32_t n=(int32_t)*v+d*step;n=n<0?0:n>max?max:n;bool ok=volt?opendps_set_voltage(&psu[c],n):opendps_set_current(&psu[c],n);if(ok)*v=(uint16_t)n;render(c);}
static void button_events(void){for(unsigned i=0;i<4;i++)if(enc[i].press){enc[i].press=false;unsigned c=i&1u;bool volt=i<2;if(!ch[c].connected)continue;channel_mode_t m=ch[c].mode;if(volt){if(m==OUT_OFF)ch[c].mode=OUT_OFF_V_STEP_CHG;else if(m==OUT_ON)ch[c].mode=OUT_ON_V_STEP_CHG;else if(m==OUT_OFF_V_STEP_CHG)ch[c].mode=OUT_OFF;else if(m==OUT_ON_V_STEP_CHG)ch[c].mode=OUT_ON;}else{if(m==OUT_OFF)ch[c].mode=OUT_OFF_I_STEP_CHG;else if(m==OUT_ON)ch[c].mode=OUT_ON_I_STEP_CHG;else if(m==OUT_OFF_I_STEP_CHG)ch[c].mode=OUT_OFF;else if(m==OUT_ON_I_STEP_CHG)ch[c].mode=OUT_ON;}render(c);}for(unsigned c=0;c<2;c++)if(power_press[c]){power_press[c]=false;if(!ch[c].connected||regs.locked[c]||(ch[c].mode!=OUT_OFF&&ch[c].mode!=OUT_ON))continue;bool on=ch[c].mode==OUT_OFF;if(opendps_set_output(&psu[c],on)){regs.output_en[c]=on;ch[c].mode=on?OUT_ON:OUT_OFF;render(c);}}if(wifi_press){wifi_press=false;reply(CORE0_WIFI_TOGGLE,0);}}

static enum intercore_command pick(unsigned c,enum intercore_command a,enum intercore_command b){return c?a:b;}
static void core_command(uint32_t m){enum intercore_command x=m>>24;uint16_t v=(uint16_t)m;unsigned c;
 if(x==CORE1_WIFI_STATE){wifi_state=(enum wifi_led_state)v;update_leds();if(v==WIFI_LED_CONNECTING){write_string(&seg[0],"  WiFi  ",8,0,7);write_string(&seg[0],"  WiFi  ",8,8,15);write_string(&seg[1],"connect ",8,0,7);write_string(&seg[1],"connect ",8,8,15);}else if(v==WIFI_LED_OK||v==WIFI_LED_ERROR){write_string(&seg[0],"  WiFi  ",8,0,7);write_string(&seg[0],"  WiFi  ",8,8,15);write_string(&seg[1],v==WIFI_LED_OK?"   OK   ":"!!Fail!!",8,0,7);write_string(&seg[1],v==WIFI_LED_OK?"   OK   ":"!!Fail!!",8,8,15);}status_until=make_timeout_time_ms(v==WIFI_LED_CONNECTING?30000:1000);status_active=v!=WIFI_LED_OFF;return;}
 if(x>=CORE1_SET_INT2_VOLTAGE_LIM&&x<=CORE0_DAT_INT2_VOLTAGE_LIM)c=1;else if(x>=CORE1_SET_INT2_CURRENT_LIM&&x<=CORE0_DAT_INT2_CURRENT_LIM)c=1;else if(x>=CORE1_SET_INT2_OUTPUT&&x<=CORE0_DAT_INT2_OUTPUT)c=1;else if(x>=CORE1_SET_INT2_LOCK&&x<=CORE0_DAT_INT2_LOCK)c=1;else if(x>=CORE1_SET_INT2_REMOTE&&x<=CORE0_DAT_INT2_REMOTE)c=1;else c=0;
 bool setv=x==CORE1_SET_INT1_VOLTAGE_LIM||x==CORE1_SET_INT2_VOLTAGE_LIM,seti=x==CORE1_SET_INT1_CURRENT_LIM||x==CORE1_SET_INT2_CURRENT_LIM,seto=x==CORE1_SET_INT1_OUTPUT||x==CORE1_SET_INT2_OUTPUT;
 if((setv||seti||seto)&&regs.locked[c]){reply(CORE0_ERR_INTERFACE_LOCKED,c+1);return;}bool ok=true;if(setv)ok=opendps_set_voltage(&psu[c],v),regs.voltage_lim[c]=ok?v:regs.voltage_lim[c];else if(seti)ok=opendps_set_current(&psu[c],v),regs.current_lim[c]=ok?v:regs.current_lim[c];else if(seto)ok=opendps_set_output(&psu[c],v!=0),regs.output_en[c]=ok?(v!=0):regs.output_en[c];else if(x==CORE1_SET_INT1_LOCK||x==CORE1_SET_INT2_LOCK)regs.locked[c]=v!=0;else if(x==CORE1_SET_INT1_REMOTE||x==CORE1_SET_INT2_REMOTE)regs.remote[c]=v!=0;
 if(!ok){reply(CORE0_ERR_PSU_COMM,c+1);return;}if(setv||seti||seto)render(c);update_leds();
 if(x==CORE1_GET_INT1_VOLTAGE_LIM||x==CORE1_GET_INT2_VOLTAGE_LIM)reply(pick(c,CORE0_DAT_INT2_VOLTAGE_LIM,CORE0_DAT_INT1_VOLTAGE_LIM),regs.voltage_lim[c]);
 else if(x==CORE1_GET_INT1_CURRENT_LIM||x==CORE1_GET_INT2_CURRENT_LIM)reply(pick(c,CORE0_DAT_INT2_CURRENT_LIM,CORE0_DAT_INT1_CURRENT_LIM),regs.current_lim[c]);
 else if(x==CORE1_GET_INT1_OUTPUT||x==CORE1_GET_INT2_OUTPUT)reply(pick(c,CORE0_DAT_INT2_OUTPUT,CORE0_DAT_INT1_OUTPUT),regs.output_en[c]);
 else if(x==CORE1_GET_INT1_LOCK||x==CORE1_GET_INT2_LOCK)reply(pick(c,CORE0_DAT_INT2_LOCK,CORE0_DAT_INT1_LOCK),regs.locked[c]);
 else if(x==CORE1_GET_INT1_REMOTE||x==CORE1_GET_INT2_REMOTE)reply(pick(c,CORE0_DAT_INT2_REMOTE,CORE0_DAT_INT1_REMOTE),regs.remote[c]);}

static void core1_main(void){spi_init(spi0,2250000);spi_set_format(spi0,8,SPI_CPOL_0,SPI_CPHA_0,SPI_MSB_FIRST);gpio_set_function(WIRECTL_SPI_SCK,GPIO_FUNC_SPI);gpio_set_function(WIRECTL_SPI_TX,GPIO_FUNC_SPI);led14seg_init(&seg[0],spi0,WIRECTL_SPI_SCK,WIRECTL_SPI_TX,WIRECTL_SEG1_CS);led14seg_init(&seg[1],spi0,WIRECTL_SPI_SCK,WIRECTL_SPI_TX,WIRECTL_SEG2_CS);gpio_init(WIRECTL_LED_CS);gpio_set_dir(WIRECTL_LED_CS,GPIO_OUT);gpio_put(WIRECTL_LED_CS,1);leds_write();led14seg_blink_timer_init();controls_init();opendps_set_idle_hook(service_displays);opendps_init(&psu[0],uart0,WIRECTL_UART0_TX,WIRECTL_UART0_RX);opendps_init(&psu[1],uart1,WIRECTL_UART1_TX,WIRECTL_UART1_RX);multicore_fifo_clear_irq();irq_set_exclusive_handler(SIO_FIFO_IRQ_NUM(1),fifo_irq1);irq_set_enabled(SIO_FIFO_IRQ_NUM(1),true);
 for(unsigned c=0;c<2;c++){ch[c].vstep=100;ch[c].istep=10;ch[c].connected=opendps_query(&psu[c],250);ch[c].mode=ch[c].connected?(psu[c].output_enabled?OUT_ON:OUT_OFF):OUT_PSU_CONN_FAULT;if(ch[c].connected){regs.voltage[c]=psu[c].voltage;regs.current[c]=psu[c].current;regs.voltage_lim[c]=psu[c].voltage_set;regs.current_lim[c]=psu[c].current_set;regs.output_en[c]=psu[c].output_enabled;}render(c);}update_leds();absolute_time_t next=make_timeout_time_ms(100);
 for(;;){while(r1!=w1){uint32_t m=q1[r1];r1=(r1+1)&31;core_command(m);}if(status_active&&time_reached(status_until)){status_active=false;render(0);render(1);}for(unsigned i=0;i<4;i++){int d=enc[i].turns;if(d){enc[i].turns=0;apply_turn(i,d>0?1:-1);}}button_events();if(time_reached(next)){next=make_timeout_time_ms(100);for(unsigned c=0;c<2;c++)if(ch[c].connected&&opendps_query(&psu[c],60)){regs.voltage[c]=psu[c].voltage;regs.current[c]=psu[c].current;regs.voltage_lim[c]=psu[c].voltage_set;regs.current_lim[c]=psu[c].current_set;regs.output_en[c]=psu[c].output_enabled;regs.current_lim_trg[c]=opendps_is_cc(&psu[c]);if(!status_active)render(c);}update_leds();}redraw_display(&seg[0]);redraw_display(&seg[1]);sleep_us(300);}}

static bool wifi_on;
static bool wifi_start(void){intercore_send(CORE1_WIFI_STATE,WIFI_LED_CONNECTING);cyw43_arch_enable_sta_mode();if(cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID,WIFI_PASSWORD,CYW43_AUTH_WPA2_AES_PSK,30000)){intercore_send(CORE1_WIFI_STATE,WIFI_LED_ERROR);return false;}intercore_send(CORE1_WIFI_STATE,WIFI_LED_OK);wifi_on=true;return start_scpi_tcp_server();}
int main(void){stdio_init_all();multicore_launch_core1(core1_main);multicore_fifo_clear_irq();irq_set_exclusive_handler(SIO_FIFO_IRQ_NUM(0),fifo_irq0);irq_set_enabled(SIO_FIFO_IRQ_NUM(0),true);bool radio_ok=cyw43_arch_init()==0;if(radio_ok)(void)wifi_start();else intercore_send(CORE1_WIFI_STATE,WIFI_LED_ERROR);for(;;){uint32_t m;while(intercore_core0_try_pop(&m)){if((m>>24)==CORE0_WIFI_TOGGLE&&radio_ok){if(wifi_on){cyw43_arch_disable_sta_mode();wifi_on=false;intercore_send(CORE1_WIFI_STATE,WIFI_LED_OFF);}else(void)wifi_start();}else scpi_handle_intercore(m);}if(wifi_on)poll_scpi_tcp_server();sleep_ms(1);}}
