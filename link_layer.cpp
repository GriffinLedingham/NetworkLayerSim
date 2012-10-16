#include "link_layer.h"
#include "timeval_operators.h"

unsigned short checksum(struct Packet);
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

Link_layer::Link_layer(Physical_layer_interface* physical_layer_interface,
 unsigned int num_sequence_numbers,
 unsigned int max_send_window_size,unsigned int timeout)
{
	this->physical_layer_interface = physical_layer_interface;

	receive_buffer_length = 0;
    
    next_send_seq = 0;
    next_send_ack = 0;
    next_receive_seq = 0;
    last_receive_ack = 0;
    send_queue_size = 0;
    
    start = 0;
    end = 0;
        
    timeval_timeout.tv_usec = timeout;

	if (pthread_create(&thread,NULL,&Link_layer::loop,this) < 0) {
		throw Link_layer_exception();
	}
}

unsigned int Link_layer::send(unsigned char buffer[],unsigned int length)
{
	unsigned int n = physical_layer_interface->send(buffer,length);

	return n;
}

unsigned int Link_layer::receive(unsigned char buffer[])
{
    unsigned int N = receive_buffer;
    if(N != 0)
    {
        for(unsigned int i = 0;i<N;i++)
        {
            buffer[i] = receive_buffer[i];
        }
        receive_buffer_length = 0;
        return N;
    }
    else
    {
        return 0;
    }
}

void Link_layer::process_received_packet(struct Packet p)
{
}

void Link_layer::remove_acked_packets()
{
}

void Link_layer::send_timed_out_packets()
{
}

void Link_layer::generate_ack_packet()
{
}

void* Link_layer::loop(void* thread_creator)
{
	const unsigned int LOOP_INTERVAL = 10;
	Link_layer* link_layer = ((Link_layer*) thread_creator);

	while (true) {
		if (link_layer->receive_buffer_length == 0) {
			unsigned int length =
			 link_layer->physical_layer_interface->receive
			 (link_layer->receive_buffer);

			if (length > 0) {
				link_layer->receive_buffer_length = length;
			}
		}

		usleep(LOOP_INTERVAL);
	}

	return NULL;
}

// this is the standard Internet checksum algorithm
unsigned short checksum(struct Packet p)
{
	unsigned long sum = 0;
	struct Packet copy;
	unsigned short* shortbuf;
	unsigned int length;

	if (p.header.data_length > Link_layer::MAXIMUM_DATA_LENGTH) {
		throw Link_layer_exception();
	}

	copy = p;
	copy.header.checksum = 0;
	length = sizeof(Packet_header)+copy.header.data_length;
	shortbuf = (unsigned short*) &copy;

	while (length > 1) {
		sum += *shortbuf++;
		length -= 2;
	}
	// handle the trailing byte, if present
	if (length == 1) {
		sum += *(unsigned char*) shortbuf;
	}

	sum = (sum >> 16)+(sum & 0xffff);
	sum = (~(sum+(sum >> 16)) & 0xffff);
	return (unsigned short) sum;
}
