#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/tcp.h>
#include <linux/moduleparam.h>
#include <linux/tcp.h>

static int initial_ssthresh = 21;
static int tikic_tcp_max_ssthresh = 25;

module_param(initial_ssthresh, int, S_IRUSR | S_IWUSR | S_IXUSR);
MODULE_PARM_DESC(initial_ssthresh, "initial value of slow start threshold");
module_param(tikic_tcp_max_ssthresh, int, S_IRUSR | S_IWUSR | S_IXUSR);
MODULE_PARM_DESC(tikic_tcp_max_ssthresh, "maximal value of slow start threshold");

static void tikic_init(struct sock *sk){
    //printk(KERN_INFO "tikic_init\n");
    /* initial ssthresh */
    if(initial_ssthresh)
        tcp_sk(sk)->snd_ssthresh = initial_ssthresh;
}

/*
 * Slow start is used when congestion window is less than slow start 
 * threshold. 
 *
 */
static void tikic_slow_start(struct tcp_sock* tp){
    
    printk(KERN_INFO "tikic_slow_start\n");
    printk(KERN_INFO "before snd_cwnd: %d\n", tp->snd_cwnd);
    unsigned int cnt = 0; /* increase in packets */
    unsigned int delta = 0;
    u32 snd_cwnd = tp->snd_cwnd;

    if(tikic_tcp_max_ssthresh > 0 && tp->snd_cwnd > tikic_tcp_max_ssthresh){
        cnt = tikic_tcp_max_ssthresh >> 1;
    }else{
        cnt = snd_cwnd;
    }
    tp->snd_cwnd_cnt += cnt;
    while( tp->snd_cwnd_cnt >= snd_cwnd ){
        tp->snd_cwnd_cnt -= snd_cwnd;
        delta++;
    }
    tp->snd_cwnd = min(snd_cwnd + delta, tp->snd_cwnd_clamp);
    printk(KERN_INFO "after snd_cwnd: %d\n", tp->snd_cwnd);
}

/* In theory this is tp->snd_cwnd += 1 / tp->snd_cwnd (or alternative w) */
static void tikic_tcp_cong_avoid_ai(struct tcp_sock *tp, u32 w)
{
    printk(KERN_INFO "tikic_tcp_cong_avoid_ai\n");
    printk(KERN_INFO "before snd_cwnd: %d\n", tp->snd_cwnd);
    
	if (tp->snd_cwnd_cnt >= w) {
		if (tp->snd_cwnd < tp->snd_cwnd_clamp)
			tp->snd_cwnd++;
		tp->snd_cwnd_cnt = 0;
	} else {
		tp->snd_cwnd_cnt++;
	}
    printk(KERN_INFO "after snd_cwnd: %d\n", tp->snd_cwnd);
}

static void tikic_cong_avoid(struct sock *sk, u32 ack, u32 in_flight){
    struct tcp_sock *tp = tcp_sk(sk);
    printk(KERN_INFO "tikic_cong_avoid\n");
    
    /* if(!tcp_is_cwnd_limited(sk, in_flight)){
        return;
    }*/

    /* In "safe" area, increase. */
    if(tp->snd_cwnd <= tp->snd_ssthresh)
        tikic_slow_start(tp);
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
    .owner      = THIS_MODULE,
    .name       = "tikic",
};

static int __init tikictcp_register(void){
    printk(KERN_INFO "tikictcp_register1111\n");
    return tcp_register_congestion_control(&tikictcp);
}

static void __exit tikictcp_unregister(void){
    printk(KERN_INFO "tikictcp_unregister\n");
    tcp_unregister_congestion_control(&tikictcp);
}

module_init(tikictcp_register);
module_exit(tikictcp_unregister);

MODULE_AUTHOR("KiLiu");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TIKIC TCP");
MODULE_VERSION("0.1");