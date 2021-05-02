#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

/*模拟协议5，gobanckn*/

#define DATA_TIMER  2000
#define ACK_TIMER 240
#define MAX_SEQ 7 //发送窗口最大值

//定义每一帧的数据结构
struct FRAME {
	unsigned char kind; /* FRAME_DATA ，帧的种类*/
	unsigned char ack;
	unsigned char seq;
	unsigned char data[PKT_LEN];
	unsigned int  padding;
};
//定义发送和接受窗口
static unsigned char next_frame_to_send = 0;//窗口的上边界+1
static unsigned char ack_expected = 0;//窗口下边界

//static unsigned char frame_nr = 0;//发送的帧的序号

static unsigned char out_buffer[MAX_SEQ + 1][PKT_LEN];//发送缓存
static unsigned char in_buffer[PKT_LEN];//接受缓存
static unsigned char nbuffered = 0;//指明当前发送缓存的数目
static unsigned char frame_expected = 0;//接受窗口的位置
static int phl_ready = 0;
static int no_nak = 1;



//给数据帧添加CRC检验码并向物理层发送此帧
static void put_frame(unsigned char* frame, int len)
{
	*(unsigned int*)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	phl_ready = 0;
}

//判断收到的ack是否在发送窗口内
int between(unsigned char a, unsigned char b, unsigned char c)
{
	if (((a <= b) && (b < c)) || ((c < a) && (a <= b)) || ((b < c) && (c < a)))
		return 1;
	else
		return 0;
}
//发送三种帧，data/ack/nak
static void send_3_frame(unsigned char kind, unsigned char frame_nr, unsigned char frame_expected, unsigned char buffer[][PKT_LEN])
{
	struct FRAME s;

	s.kind = kind;
	//s.seq = frame_nr;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);//决定回复用的ack
	//memcpy(s.data, buffer, PKT_LEN);
	//发送数据帧
	if (kind == FRAME_DATA)
	{
		s.seq = frame_nr;//数据帧的序号
		memcpy(s.data, buffer[frame_nr], PKT_LEN);//给数据域赋值
		dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short*)s.data);
		put_frame((unsigned char*)&s, 3 + PKT_LEN);//多了三个控制字节，将帧传递给物理层
		start_timer(frame_nr, DATA_TIMER);//对这个序号的帧开启定时器
	}
	//发送ACK帧
	else if (kind == FRAME_ACK)
	{
		dbg_frame("Send ACK  %d\n", s.ack);
		put_frame((unsigned char*)&s, 2);//单独发送的ack只有kind和ack字段，长度为2
	}
	//发送NAK帧
	else if (kind == FRAME_NAK)
	{
		no_nak = 0;
		dbg_frame("Send NAK  %d\n", s.ack);
		put_frame((unsigned char*)&s, 2);//单独发送的nak只有kind和ack字段，长度为2
	}
	phl_ready = 0;//物理层不可用
	stop_ack_timer();//ack计时器可以停止了
}


int main(int argc, char** argv)
{
	int event, arg;
	struct FRAME f;
	int len = 0;

	protocol_init(argc, argv);//环境初始化
	lprintf("Designed by WZH&&CMH, build: " __DATE__"  "__TIME__"\n");

	enable_network_layer();//使物理层可用

	while (1)
	{
		event = wait_for_event(&arg);

		switch (event)
		{
		case NETWORK_LAYER_READY://网络层有要发送的分组
			dbg_event("NETWORK_LAYER_READY;\n");
			get_packet(out_buffer[next_frame_to_send]);//从网络层接收一个数据包
			nbuffered++;//缓存数目+1
			dbg_frame("----SEND DATA %d %d ;\n", next_frame_to_send, (frame_expected + MAX_SEQ) % (MAX_SEQ + 1));
			send_3_frame(FRAME_DATA, next_frame_to_send, frame_expected, out_buffer);//发送帧
			next_frame_to_send = (next_frame_to_send + 1) % (MAX_SEQ + 1);//下一个待发送帧的序号
			break;

		case PHYSICAL_LAYER_READY:
			phl_ready = 1;
			break;

		case FRAME_RECEIVED://收到帧
			dbg_event("FRAME_RECEIVED;\n");
			len = recv_frame((unsigned char*)&f, sizeof f);//从物理层收到一帧，len为此帧的长度
			if (len < 5 || crc32((unsigned char*)&f, len) != 0)//如果发现CRC错误
			{
				dbg_event("**** Receiver Error, Bad CRC Checksum\n");
				if (no_nak)//还没有发送过nak
				{
					dbg_frame("----SEND NAK %d;\n", (frame_expected + MAX_SEQ) % (MAX_SEQ + 1));
					send_3_frame(FRAME_NAK, 0, frame_expected, out_buffer);
				}
				break;
			}

			while (between(ack_expected, f.ack, next_frame_to_send))//收到的帧的ack序号落在已发送且未被确认的范围内，发送窗口向前滑动n格
			{
				nbuffered--;//缓存数目-1
				stop_timer(ack_expected);//停止ack帧定时器
				ack_expected = (ack_expected + 1) % (MAX_SEQ + 1);
			}

			if (f.kind == FRAME_DATA)//如果收到的是数据帧
			{
				dbg_frame("----REVEIVE DATA %d %d, ID %d\n", f.seq, f.ack, *(short*)f.data);
				if (f.seq != frame_expected && no_nak == 1)//如果收到的帧没有落在接收窗口内，而且没有标记过nak
				{
					dbg_frame("----SEND NAK %d;\n", (frame_expected + MAX_SEQ) % (MAX_SEQ + 1));
					send_3_frame(FRAME_NAK, 0, frame_expected, out_buffer);
				}
				else
				{
					start_ack_timer(ACK_TIMER);
				}
				if (f.seq == frame_expected)//收到的帧落在接收窗口内
				{
					dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short*)f.data);
					memcpy(in_buffer, f.data, len - 7);//减去三位控制字段和四位校验和字段
					put_packet(in_buffer, len - 7);//上交给网络层
					no_nak = 1;
					frame_expected = (frame_expected + 1) % (MAX_SEQ + 1);
					start_ack_timer(ACK_TIMER);
				}
			}
			if (f.kind == FRAME_NAK && between(ack_expected, (f.ack + 1) % (MAX_SEQ + 1), next_frame_to_send))//收到的是NAK帧，缓存中的帧要全部重发
			{
				int i;
				dbg_frame("RECEIVE NAK %dn\n", f.ack);
				next_frame_to_send = ack_expected;//回退n步
				for (i = 0; i < nbuffered; i++)
				{
					send_3_frame(FRAME_DATA, next_frame_to_send, frame_expected, out_buffer);
					next_frame_to_send = (next_frame_to_send + 1) % (MAX_SEQ + 1);
				}
			}
			if (f.kind == FRAME_ACK)//收到的是单独的一个ACK帧
			{
				dbg_frame("RECEIVE ACK %d\n", f.ack);
			}
			break;

		case DATA_TIMEOUT://数据超时，需要回退n步全部重传
			dbg_event("----DATA %d timeout\n", arg);//参数arg会返回超时的帧的序号
			next_frame_to_send = ack_expected;//回退n步
			int i;
			for (i = 0; i < nbuffered; i++)
			{
				send_3_frame(FRAME_DATA, next_frame_to_send, frame_expected, out_buffer);
				next_frame_to_send = (next_frame_to_send + 1) % (MAX_SEQ + 1);
			}
			break;

		case ACK_TIMEOUT://ACK超时，即在一定时间内没有返回的流量，ack因此没有被捎带返回
			dbg_event("----ACK %d timeout\n", arg);//参数arg会返回超时的帧的序号
			send_3_frame(FRAME_ACK, 0, frame_expected, out_buffer);
			break;
		}

		if (nbuffered < MAX_SEQ && phl_ready)//如果物理层准备好了而且缓存的数量小于最大窗口
			enable_network_layer();
		else
			disable_network_layer();
	}

}
