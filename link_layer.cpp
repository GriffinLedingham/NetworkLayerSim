#include "link_layer.h"
#include "timeval_operators.h"

unsigned short checksum(struct Packet);
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
unsigned int numseq;

Link_layer::Link_layer(Physical_layer_interface* physical_layer_interface,
                       unsigned int num_sequence_numbers,
                       unsigned int max_send_window_size,unsigned int timeout)
{
    this->physical_layer_interface = physical_layer_interface;
    
    limit = max_send_window_size;
    
    numseq = num_sequence_numbers;
    
    receive_buffer_length = 0;
    
    next_send_seq = 0;
    next_receive_seq = 0;
    last_receive_ack = 0;
    
    send_queue_size = 0;
    
    start = 0;
    end = 0;
    
    timeval_timeout.tv_usec = timeout;
    
    if (pthread_create(&thread,NULL,&Link_layer::loop,this) < 0)
    {
        throw Link_layer_exception();
    }
}

unsigned int Link_layer::send(unsigned char buffer[],unsigned int length)
{
    if (length == 0 || length >MAXIMUM_DATA_LENGTH)
    {
        throw Link_layer_exception();
    }
    pthread_mutex_lock(&mutex);
    //if((start == end || (start != end && start%limit != end %limit))&& send_queue_size < limit)
    if(send_queue_size < limit)
    {
        struct Timed_packet P;
        
        gettimeofday(&P.send_time,NULL);
        
        for(unsigned int i=0;i<length;i++)
        {
            P.packet.data[i] = buffer[i];
        }
        P.packet.header.data_length = length;
        P.packet.header.seq = next_send_seq;
        
        send_queue.push_back(P);
        send_queue_size++;
        next_send_seq = (1+next_send_seq)%numseq;
        pthread_mutex_unlock(&mutex);
        return length;
    }
    else
    {
        pthread_mutex_unlock(&mutex);
        return 0;
    }
}

unsigned int Link_layer::receive(unsigned char buffer[])
{
    pthread_mutex_lock(&mutex);
    unsigned int N = receive_buffer_length;
    if(N > 0)
    {
        for(unsigned int i = 0;i<N;i++)
        {
            buffer[i] = receive_buffer[i];
        }
        receive_buffer_length = 0;
        //cout<<"received! N= " << N << "\n";
        pthread_mutex_unlock(&mutex);
        return N;
    }
    else
    {
        pthread_mutex_unlock(&mutex);
        return 0;
    }
}

void Link_layer::process_received_packet(struct Packet p)
{
    if(p.header.seq == next_receive_seq)
    {
        if (p.header.data_length > 0)
        {
            if(receive_buffer_length == 0)
            {
                for(unsigned int i = 0; i < p.header.data_length; i++)
                {
                    receive_buffer[i] = p.data[i];
                }
                receive_buffer_length = p.header.data_length;
                //cout<<"process received! buf length: " << receive_buffer_length << "\n";
                next_receive_seq = (1+next_receive_seq)%numseq;
            }
        }
        else
        {
            next_receive_seq = (1+next_receive_seq)%numseq;
        }
    }
    
    last_receive_ack = p.header.ack;
    
}

void Link_layer::remove_acked_packets()
{
    if(send_queue_size >0 )
    {
        unsigned int i=1;
        for (deque<Timed_packet>::iterator j = send_queue.begin(); j != send_queue.end(); j++)
        {
            
            if ((*j).packet.header.seq == last_receive_ack - 1)
            {

                send_queue.erase(send_queue.begin(), send_queue.begin() + i);
                send_queue_size-=(i);
                break;
            }
            i++;
        }
    }
}

void Link_layer::send_timed_out_packets()
{
    Timed_packet P;
    for (deque<Timed_packet>::iterator j = send_queue.begin(); j != send_queue.end(); j++)
    {
        timeval current;
        gettimeofday(&current,NULL);
        if ((*j).send_time < current)
        {
            (*j).packet.header.ack = next_receive_seq;
            (*j).packet.header.checksum = checksum((*j).packet);
            if (physical_layer_interface->send((unsigned char *)&((*j).packet), sizeof((*j).packet)) != 0)
            {
                gettimeofday(&current,NULL);
                (*j).send_time.tv_usec = current.tv_usec + timeval_timeout.tv_usec;
            }
        }
    }
}

void Link_layer::generate_ack_packet()
{
    if(send_queue_size == 0)
    {
        Timed_packet P;
        gettimeofday(&(P.send_time),NULL);
        P.packet.header.seq = next_send_seq;
        P.packet.header.data_length = 0;
        next_send_seq = (1+next_send_seq)%numseq;
        send_queue.push_back(P);
        send_queue_size++;
    }
}

void* Link_layer::loop(void* thread_creator)
{
    const unsigned int LOOP_INTERVAL = 10;
    Link_layer* link_layer = ((Link_layer*) thread_creator);
    Packet P;
    
    while (true)
    {
        unsigned int length = link_layer->physical_layer_interface->receive((unsigned char*)&P);
        if(length > 0)
        {
            unsigned int N = P.header.data_length + sizeof(struct Packet_header);//length;
            if(N >= HEADER_LENGTH
               && N <= HEADER_LENGTH + MAXIMUM_DATA_LENGTH
               && P.header.data_length <= MAXIMUM_DATA_LENGTH
               && P.header.checksum == checksum(P))
            {
                pthread_mutex_lock(&mutex);
                link_layer->process_received_packet(P);
                pthread_mutex_unlock(&mutex);
                
            }
        }
        
        pthread_mutex_lock(&mutex);
        link_layer->remove_acked_packets();
        link_layer->send_timed_out_packets();
        pthread_mutex_unlock(&mutex);
        
        usleep(LOOP_INTERVAL);
        
        pthread_mutex_lock(&mutex);
        link_layer->generate_ack_packet();
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

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