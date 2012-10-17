#include "link_layer.h"
#include "timeval_operators.h"

unsigned short checksum(struct Packet);
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

Link_layer::Link_layer(Physical_layer_interface* physical_layer_interface,
                       unsigned int num_sequence_numbers,
                       unsigned int max_send_window_size,unsigned int timeout)
{
    this->physical_layer_interface = physical_layer_interface;
    
    limit = max_send_window_size;
    
    
    receive_buffer_length = 0;
    
    next_send_seq = 0;
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
    if (length == 0 || length >MAXIMUM_DATA_LENGTH)
    {
        throw Link_layer_exception();
    }
    //pthread_mutex_lock(&mutex);
    if(start == end || (start != end && start%limit != end %limit))
    {
        struct Timed_packet P;
        
        gettimeofday(&P.send_time,NULL);
        
        for(unsigned int i=0;i<length;i++)
        {
            P.packet.data[i] = buffer[i];
        }
        P.packet.header.data_length = length;
        P.packet.header.seq = next_send_seq;
        send_queue[start] = P;
        start = (start++) % limit ;
        next_send_seq++;
        send_queue_size++;
        //pthread_mutex_unlock(&mutex);
        return length;
    }
    else
    {
        //pthread_mutex_unlock(&mutex);
        return 0;
    }
}

unsigned int Link_layer::receive(unsigned char buffer[])
{
    unsigned int N = receive_buffer_length;
    if(N > 0)
    {
        for(unsigned int i = 0;i<N;i++)
        {
            buffer[i] = receive_buffer[i];
        }
        receive_buffer_length = 0;
        cout<<"received!\n";
        return N;
    }
    else
    {
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
                next_receive_seq++;
            }
        }else
        {
            next_receive_seq++;
        }
    }
    last_receive_ack = p.header.ack;
    
}

void Link_layer::remove_acked_packets()
{
    
    if(send_queue_size >0 )
    {
        for(unsigned int i = 0;i<send_queue_size;i++)
        {
            Timed_packet p = send_queue[(end+i)%limit];
            if(p.packet.header.seq >= last_receive_ack && p.packet.header.data_length >0)
            {
                //cout<< "\npack seq "<<p.packet.header.seq<<"\n";
                send_queue_size -= (i+1);
                end = (end+1+i)%limit;
                return;
            }
        }
    }
}

void Link_layer::send_timed_out_packets()
{
    Timed_packet P;
    for(unsigned int i = 0;i<send_queue_size;i++)
    {
        timeval current;
        gettimeofday(&current,NULL);
        P = send_queue[(end+i)%limit];
        if(P.send_time < current && P.packet.header.data_length > 0)
        {
            P.packet.header.ack = next_receive_seq;
            P.packet.header.checksum = checksum(P.packet);
            if(physical_layer_interface->send((unsigned char*)&(P),(P.packet.header.data_length + sizeof(P.packet.header))))
            {
                gettimeofday(&current,NULL);
                P.send_time = current + timeval_timeout;
                cout<< "Send Time Out\n";
            }
            send_queue[(end+i)%limit] = P;
        }
    }
}

void Link_layer::generate_ack_packet()
{
    if(send_queue_size == 0 && start== end)
    {
        Timed_packet P;
        gettimeofday(&(P.send_time),NULL);
        P.packet.header.seq = next_send_seq;
        P.packet.header.data_length = 0;
        next_send_seq++;
        send_queue[start] = P;
        send_queue_size++;
        start = start++ % limit;
        //cout<< "Generate "<<send_queue_size<<" start "<<start<<" end "<<end<<"\n";
    }
}

void* Link_layer::loop(void* thread_creator)
{
    const unsigned int LOOP_INTERVAL = 10;
    Link_layer* link_layer = ((Link_layer*) thread_creator);
    Packet P;
    
    while (true)
    {
        pthread_mutex_lock(&mutex);
        if(link_layer->receive_buffer_length == 0)
        {
            unsigned int length = link_layer->physical_layer_interface->receive((unsigned char*)&P);
            cout<<"length "<<length<<"\n";
            receive_buffer_length = length;
            if(receive_buffer_length != 0)
            {
                unsigned int N = P.header.data_length + sizeof(struct Packet_header);
                if(N >= HEADER_LENGTH
                   && N <= HEADER_LENGTH + MAXIMUM_DATA_LENGTH
                   && P.header.data_length <= MAXIMUM_DATA_LENGTH
                   && (checksum(P)>0))
                {
                    
                    link_layer->process_received_packet(P);
                    
                }
            }
        }
        
        
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