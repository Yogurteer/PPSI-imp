class NetworkChannel {
public:
    long long total_bytes_sent = 0;
    long long total_bytes_received = 0;

    void send(size_t data_size) {
        total_bytes_sent += data_size; // 记录这里
    }
    
    // ... recv 同理
    void recv(size_t data_size) {
        total_bytes_received += data_size; // 记录这里
    }
};