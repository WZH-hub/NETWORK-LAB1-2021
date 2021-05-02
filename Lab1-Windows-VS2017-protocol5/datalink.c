#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

/*ģ��Э��5��gobanckn*/

#define DATA_TIMER  2000
#define ACK_TIMER 240
#define MAX_SEQ 7 //���ʹ������ֵ

//����ÿһ֡�����ݽṹ
struct FRAME {
	unsigned char kind; /* FRAME_DATA ��֡������*/
	unsigned char ack;
	unsigned char seq;
	unsigned char data[PKT_LEN];
	unsigned int  padding;
};
//���巢�ͺͽ��ܴ���
static unsigned char next_frame_to_send = 0;//���ڵ��ϱ߽�+1
static unsigned char ack_expected = 0;//�����±߽�

//static unsigned char frame_nr = 0;//���͵�֡�����

static unsigned char out_buffer[MAX_SEQ + 1][PKT_LEN];//���ͻ���
static unsigned char in_buffer[PKT_LEN];//���ܻ���
static unsigned char nbuffered = 0;//ָ����ǰ���ͻ������Ŀ
static unsigned char frame_expected = 0;//���ܴ��ڵ�λ��
static int phl_ready = 0;
static int no_nak = 1;



//������֡���CRC�����벢������㷢�ʹ�֡
static void put_frame(unsigned char* frame, int len)
{
	*(unsigned int*)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	phl_ready = 0;
}

//�ж��յ���ack�Ƿ��ڷ��ʹ�����
int between(unsigned char a, unsigned char b, unsigned char c)
{
	if (((a <= b) && (b < c)) || ((c < a) && (a <= b)) || ((b < c) && (c < a)))
		return 1;
	else
		return 0;
}
//��������֡��data/ack/nak
static void send_3_frame(unsigned char kind, unsigned char frame_nr, unsigned char frame_expected, unsigned char buffer[][PKT_LEN])
{
	struct FRAME s;

	s.kind = kind;
	//s.seq = frame_nr;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);//�����ظ��õ�ack
	//memcpy(s.data, buffer, PKT_LEN);
	//��������֡
	if (kind == FRAME_DATA)
	{
		s.seq = frame_nr;//����֡�����
		memcpy(s.data, buffer[frame_nr], PKT_LEN);//��������ֵ
		dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short*)s.data);
		put_frame((unsigned char*)&s, 3 + PKT_LEN);//�������������ֽڣ���֡���ݸ������
		start_timer(frame_nr, DATA_TIMER);//�������ŵ�֡������ʱ��
	}
	//����ACK֡
	else if (kind == FRAME_ACK)
	{
		dbg_frame("Send ACK  %d\n", s.ack);
		put_frame((unsigned char*)&s, 2);//�������͵�ackֻ��kind��ack�ֶΣ�����Ϊ2
	}
	//����NAK֡
	else if (kind == FRAME_NAK)
	{
		no_nak = 0;
		dbg_frame("Send NAK  %d\n", s.ack);
		put_frame((unsigned char*)&s, 2);//�������͵�nakֻ��kind��ack�ֶΣ�����Ϊ2
	}
	phl_ready = 0;//����㲻����
	stop_ack_timer();//ack��ʱ������ֹͣ��
}


int main(int argc, char** argv)
{
	int event, arg;
	struct FRAME f;
	int len = 0;

	protocol_init(argc, argv);//������ʼ��
	lprintf("Designed by WZH&&CMH, build: " __DATE__"  "__TIME__"\n");

	enable_network_layer();//ʹ��������

	while (1)
	{
		event = wait_for_event(&arg);

		switch (event)
		{
		case NETWORK_LAYER_READY://�������Ҫ���͵ķ���
			dbg_event("NETWORK_LAYER_READY;\n");
			get_packet(out_buffer[next_frame_to_send]);//����������һ�����ݰ�
			nbuffered++;//������Ŀ+1
			dbg_frame("----SEND DATA %d %d ;\n", next_frame_to_send, (frame_expected + MAX_SEQ) % (MAX_SEQ + 1));
			send_3_frame(FRAME_DATA, next_frame_to_send, frame_expected, out_buffer);//����֡
			next_frame_to_send = (next_frame_to_send + 1) % (MAX_SEQ + 1);//��һ��������֡�����
			break;

		case PHYSICAL_LAYER_READY:
			phl_ready = 1;
			break;

		case FRAME_RECEIVED://�յ�֡
			dbg_event("FRAME_RECEIVED;\n");
			len = recv_frame((unsigned char*)&f, sizeof f);//��������յ�һ֡��lenΪ��֡�ĳ���
			if (len < 5 || crc32((unsigned char*)&f, len) != 0)//�������CRC����
			{
				dbg_event("**** Receiver Error, Bad CRC Checksum\n");
				if (no_nak)//��û�з��͹�nak
				{
					dbg_frame("----SEND NAK %d;\n", (frame_expected + MAX_SEQ) % (MAX_SEQ + 1));
					send_3_frame(FRAME_NAK, 0, frame_expected, out_buffer);
				}
				break;
			}

			while (between(ack_expected, f.ack, next_frame_to_send))//�յ���֡��ack��������ѷ�����δ��ȷ�ϵķ�Χ�ڣ����ʹ�����ǰ����n��
			{
				nbuffered--;//������Ŀ-1
				stop_timer(ack_expected);//ֹͣack֡��ʱ��
				ack_expected = (ack_expected + 1) % (MAX_SEQ + 1);
			}

			if (f.kind == FRAME_DATA)//����յ���������֡
			{
				dbg_frame("----REVEIVE DATA %d %d, ID %d\n", f.seq, f.ack, *(short*)f.data);
				if (f.seq != frame_expected && no_nak == 1)//����յ���֡û�����ڽ��մ����ڣ�����û�б�ǹ�nak
				{
					dbg_frame("----SEND NAK %d;\n", (frame_expected + MAX_SEQ) % (MAX_SEQ + 1));
					send_3_frame(FRAME_NAK, 0, frame_expected, out_buffer);
				}
				else
				{
					start_ack_timer(ACK_TIMER);
				}
				if (f.seq == frame_expected)//�յ���֡���ڽ��մ�����
				{
					dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short*)f.data);
					memcpy(in_buffer, f.data, len - 7);//��ȥ��λ�����ֶκ���λУ����ֶ�
					put_packet(in_buffer, len - 7);//�Ͻ��������
					no_nak = 1;
					frame_expected = (frame_expected + 1) % (MAX_SEQ + 1);
					start_ack_timer(ACK_TIMER);
				}
			}
			if (f.kind == FRAME_NAK && between(ack_expected, (f.ack + 1) % (MAX_SEQ + 1), next_frame_to_send))//�յ�����NAK֡�������е�֡Ҫȫ���ط�
			{
				int i;
				dbg_frame("RECEIVE NAK %dn\n", f.ack);
				next_frame_to_send = ack_expected;//����n��
				for (i = 0; i < nbuffered; i++)
				{
					send_3_frame(FRAME_DATA, next_frame_to_send, frame_expected, out_buffer);
					next_frame_to_send = (next_frame_to_send + 1) % (MAX_SEQ + 1);
				}
			}
			if (f.kind == FRAME_ACK)//�յ����ǵ�����һ��ACK֡
			{
				dbg_frame("RECEIVE ACK %d\n", f.ack);
			}
			break;

		case DATA_TIMEOUT://���ݳ�ʱ����Ҫ����n��ȫ���ش�
			dbg_event("----DATA %d timeout\n", arg);//����arg�᷵�س�ʱ��֡�����
			next_frame_to_send = ack_expected;//����n��
			int i;
			for (i = 0; i < nbuffered; i++)
			{
				send_3_frame(FRAME_DATA, next_frame_to_send, frame_expected, out_buffer);
				next_frame_to_send = (next_frame_to_send + 1) % (MAX_SEQ + 1);
			}
			break;

		case ACK_TIMEOUT://ACK��ʱ������һ��ʱ����û�з��ص�������ack���û�б��Ӵ�����
			dbg_event("----ACK %d timeout\n", arg);//����arg�᷵�س�ʱ��֡�����
			send_3_frame(FRAME_ACK, 0, frame_expected, out_buffer);
			break;
		}

		if (nbuffered < MAX_SEQ && phl_ready)//��������׼�����˶��һ��������С����󴰿�
			enable_network_layer();
		else
			disable_network_layer();
	}

}
