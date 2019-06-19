#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/tcp.h>
#include <linux/moduleparam.h>
#include <linux/tcp.h>
#include <net/sock.h>
#include <net/inet_sock.h>

#define SBYTE 102400
#define BITRATE 1048
#define BIAS 2000

static int initial_ssthresh = 8;

module_param(initial_ssthresh, int, S_IRUSR | S_IWUSR | S_IXUSR);
MODULE_PARM_DESC(initial_ssthresh, "initial value of slow start threshold");

struct kictcp{
    u32 initseq;   /* first seq */
    // u32 lastseq; 
    // u32 lastdata;    
    u32 datacnt;   /*  */
    u32 srt_stamp;     /* start play vedio */
    u32 minRTT;
    u32 plackdata;
    
};
static void ipv4Print(struct sock* sk){
    struct inet_sock *insk = inet_sk(sk);
    struct tcp_sock *tp = tcp_sk(sk);
    struct kictcp *ca = inet_csk_ca(sk);
    u32 daddr = insk->inet_daddr;
    u32 saddr = insk->inet_saddr;

    u32 sf = saddr % 256;
    saddr /= 256;
    u32 ss = saddr % 256;
    saddr /= 256;
    u32 st = saddr % 256;
    saddr /= 256;
    u32 sfo = saddr % 256;

    u32 df = daddr % 256;
    daddr /= 256;
    u32 ds = daddr % 256;
    daddr /= 256;
    u32 dt = daddr % 256;
    daddr /= 256;
    u32 dfo = daddr % 256;
    printk(KERN_EMERG "%d.%d.%d.%d:%u -> %d.%d.%d.%d:%u, snd_cwnd: %u snd_una: %u all data: %uB\n",
     sf, ss, st, sfo, insk->inet_dport, df, ds, dt, dfo, insk->inet_sport, tp->snd_cwnd, tp->snd_una, ca->datacnt);
}

static void tikic_init(struct sock *sk){
    //printk(KERN_INFO "tikic_init\n");
    /* initial ssthresh */
    struct kictcp *ca = inet_csk_ca(sk);
    tcp_sk(sk)->snd_cwnd = 2;
    if(initial_ssthresh)
        tcp_sk(sk)->snd_ssthresh = initial_ssthresh;
    ca->initseq = 0;
    ca->datacnt = 0;
    ca->minRTT = 0x7fffffff;
    ca->plackdata = 0;
}

static void tikic_update(struct sock* sk, u32 bitrate){
    struct tcp_sock *tp = tcp_sk(sk);
    struct kictcp *ca = inet_csk_ca(sk);
    struct inet_connection_sock *ick = inet_csk(sk);
    u32 target_data;      /* 理想发送数据 */
    u32 target_cwnd;      /* 理想发送窗口 */
    u32 timeoff;
    //u32 diff;

    timeoff = tp->rcv_tstamp - ca->srt_stamp;       /* 视频开始播放到现在所经历的时间 */
    target_data = bitrate * timeoff + ca->plackdata;                /* 理想的数据发送量 */
    
    /* ( target_cwnd * tp->advmss * minRTT) + ca->datacnt = bitrate * (timeoff + minRTT) + bias */
    if(ca->datacnt < target_data){ 
        /* ( target_cwnd * tp->advmss * catch_up_rate(small_value) * minRTT) + ca->datacnt = bitrate * (timeoff + catch_up_rate(small_value) * minRTT) + bias  */
        target_cwnd = (target_data + bitrate*( 2 * ca->minRTT) - ca->datacnt - BIAS)/(tp->advmss * 2 * ca->minRTT);
        
    }else{
        /* ( target_cwnd * tp->advmss * catch_up_rate(small_value) * minRTT) + ca->datacnt = bitrate * (timeoff + catch_up_rate(small_value) * minRTT) + bias */
        target_cwnd = (target_data + bitrate*( 4 * ca->minRTT) - ca->datacnt - BIAS)/(tp->advmss * 4 * ca->minRTT);
    }
    printk(KERN_INFO "target data:%u target cwnd:%d \n", target_data, target_cwnd);
    //tp->snd_cwnd = max(target_cwnd, tp->snd_cwnd);
}

static void tikic_pkts_acked(struct sock* sk, u32 num_acked, s32 rtt_us){
    
    struct tcp_sock * tp = tcp_sk(sk);
    struct kictcp *ca = inet_csk_ca(sk);
    struct inet_connection_sock *ick = inet_csk(sk);
    
    ca->minRTT = min(ca->minRTT, rtt_us);
    
    if(ca->initseq == 0){
        ca->initseq = tp->snd_una;
    }else{
        ca->datacnt = tp->snd_una - ca->initseq;
    }
    
    if(ca->srt_stamp){                    /* 视频已经开始播放 */
        tikic_update(sk, BITRATE);
    }
    else if(ca->datacnt >= SBYTE){        /* 视频刚开始播放 */
        ca->srt_stamp = tp->rcv_tstamp;   /* 设置开始播放视频的时间 */
        ca->plackdata = ca->datacnt;
    }

    ipv4Print(sk);
}


/*
 * Slow start is used when congestion window is less than slow start 
 * threshold. 
 *
 */
static void tikic_slow_start(struct tcp_sock* tp, u32 acked){
    
    /* 新的拥塞窗口的大小等于ssthresh 和 cwnd中较小的那一个 */
    u32 cwnd = min(tp->snd_cwnd + acked, tp->snd_ssthresh);
    
    acked -= cwnd - tp->snd_cwnd;
    tp->snd_cwnd = min(cwnd, tp->snd_cwnd_clamp);
}

/* In theory this is tp->snd_cwnd += 1 / tp->snd_cwnd (or alternative w) */
static void tikic_tcp_cong_avoid_ai(struct tcp_sock *tp, u32 w)
{
    
	if (tp->snd_cwnd_cnt >= w) {
		if (tp->snd_cwnd < tp->snd_cwnd_clamp)
			tp->snd_cwnd++;
		tp->snd_cwnd_cnt = 0;
	} else {
		tp->snd_cwnd_cnt++;
	}
}

static void tikic_cong_avoid(struct sock *sk, u32 ack, u32 acked){
    struct tcp_sock *tp = tcp_sk(sk);

    //printk(KERN_EMERG "tikic_cong_avoid\n");
    //ipv4Print(sk);
    /* In "safe" area, increase. */
    if(tp->snd_cwnd < tp->snd_ssthresh)
        tikic_slow_start(tp, acked);
    /* In dangerous area, increase slowly. */
    else
        tikic_tcp_cong_avoid_ai(tp, tp->snd_cwnd);
    
}


/* Slow start threshold is half the congestion window (min 2) */
u32 tikic_ssthresh(struct sock *sk){
    const struct tcp_sock *tp = tcp_sk(sk);
    //int ret_snd_cwnd = max(tp->snd_cwnd >> 1U, 2U);
    //printk(KERN_INFO "ret_snd_cwnd %d\n", ret_snd_cwnd);
    return max(tp->snd_cwnd >> 1U, 2U);
}

static struct tcp_congestion_ops tikictcp __read_mostly = {
    .init       = tikic_init,
    .ssthresh   = tikic_ssthresh,
    .cong_avoid = tikic_cong_avoid,
    .pkts_acked = tikic_pkts_acked,
    .owner      = THIS_MODULE,
    .name       = "tikic",
};

static int __init tikictcp_register(void){
    printk(KERN_EMERG "tikictcp_register\n");
    return tcp_register_congestion_control(&tikictcp);
}

static void __exit tikictcp_unregister(void){
    printk(KERN_EMERG "tikictcp_unregister\n");
    tcp_unregister_congestion_control(&tikictcp);
}

module_init(tikictcp_register);
module_exit(tikictcp_unregister);

MODULE_AUTHOR("KiLiu");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TIKIC TCP");
MODULE_VERSION("0.1");