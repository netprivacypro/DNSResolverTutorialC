#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define DNS_PORT 53
#define DNS_QUERY_TYPE_A 1
#define DNS_QUERY_CLASS_IN 1
#define DNS_HEADER_SIZE 12

/**
 * @brief DNS Header structure used in DNS queries and responses.
 */
struct DNSHeader {
    unsigned short id;         ///< Identification number
    unsigned char rd :1;       ///< Recursion desired
    unsigned char tc :1;       ///< Truncated message
    unsigned char aa :1;       ///< Authoritative answer
    unsigned char opcode :4;   ///< Purpose of message
    unsigned char qr :1;       ///< Query/Response flag

    unsigned char rcode :4;    ///< Response code
    unsigned char cd :1;       ///< Checking disabled
    unsigned char ad :1;       ///< Authenticated data
    unsigned char z :1;        ///< Reserved
    unsigned char ra :1;       ///< Recursion available

    unsigned short q_count;    ///< Number of question entries
    unsigned short ans_count;  ///< Number of answer entries
    unsigned short auth_count; ///< Number of authority entries
    unsigned short add_count;  ///< Number of resource entries
};

/**
 * @brief DNS Question structure used in DNS queries.
 */
struct DNSQuestion {
    unsigned short qtype;  ///< Query type
    unsigned short qclass; ///< Query class
};

/**
 * @brief Converts a hostname to DNS format.
 *
 * @param dns Pointer to the buffer where the DNS formatted hostname will be stored.
 * @param host Pointer to the original hostname.
 */
void hostname_to_dns_format(unsigned char* dns, unsigned char* host) {
    int lock = 0, i;
    strcat((char*)host, ".");
    for(i = 0; i < strlen((char*)host); i++) {
        if(host[i] == '.') {
            *dns++ = i - lock;
            for(; lock < i; lock++) {
                *dns++ = host[lock];
            }
            lock++;
        }
    }
    *dns++ = '\0';
}

/**
 * @brief Reads a DNS-formatted name from the buffer.
 *
 * @param reader Pointer to the current position in the buffer.
 * @param buffer Pointer to the start of the buffer.
 * @param count Pointer to an integer that will be set to the number of bytes read.
 * @return Pointer to the allocated and formatted name string.
 */
unsigned char* read_name(unsigned char* reader, unsigned char* buffer, int* count) {
    unsigned char *name;
    unsigned int p = 0, jumped = 0, offset;
    int i , j;

    *count = 1;
    name = (unsigned char*)malloc(256);

    name[0]='\0';

    while(*reader != 0) {
        if(*reader >= 192) {
            offset = (*reader) * 256 + *(reader + 1) - 49152;
            reader = buffer + offset - 1;
            jumped = 1;
        } else {
            name[p++] = *reader;
        }

        reader = reader + 1;

        if(jumped == 0) {
            *count = *count + 1;
        }
    }

    name[p]='\0';
    if(jumped == 1) {
        *count = *count + 1;
    }

    for(i = 0; i < (int)strlen((const char*)name); i++) {
        p = name[i];
        for(j = 0; j < (int)p; j++) {
            name[i] = name[i + 1];
            i = i + 1;
        }
        name[i] = '.';
    }
    name[i-1]='\0';
    return name;
}

/**
 * @brief Main function to resolve a domain name using a specified DNS server.
 *
 * @param argc Argument count.
 * @param argv Argument vector containing the hostname and DNS server address.
 * @return 0 on success, 1 on failure.
 */
int main(int argc, char *argv[]) {
    unsigned char buf[65536], *qname, *reader;
    struct sockaddr_in a;
    struct DNSHeader *dns = NULL;
    struct DNSQuestion *qinfo = NULL;
    struct sockaddr_in dest;

    if(argc < 3) {
        printf("Usage: %s <hostname> <dns-server>\n", argv[0]);
        return 1;
    }

    // Create a socket
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(s < 0) {
        perror("Socket creation failed");
        return 1;
    }

    dest.sin_family = AF_INET;
    dest.sin_port = htons(DNS_PORT);
    dest.sin_addr.s_addr = inet_addr(argv[2]); // DNS server IP address

    // Set up the DNS header
    dns = (struct DNSHeader *)&buf;
    dns->id = (unsigned short) htons(getpid());
    dns->qr = 0; // This is a query
    dns->opcode = 0; // Standard query
    dns->aa = 0; // Not authoritative
    dns->tc = 0; // This message is not truncated
    dns->rd = 1; // Recursion Desired
    dns->ra = 0; // Recursion not available
    dns->z = 0;
    dns->ad = 0;
    dns->cd = 0;
    dns->rcode = 0;
    dns->q_count = htons(1); // We have only 1 question

    // Point to the query portion
    qname = (unsigned char*)&buf[sizeof(struct DNSHeader)];

    // Convert hostname to DNS format
    hostname_to_dns_format(qname, (unsigned char*)argv[1]);

    // Set up the question structure
    qinfo = (struct DNSQuestion*)&buf[sizeof(struct DNSHeader) + (strlen((const char*)qname) + 1)];
    qinfo->qtype = htons(DNS_QUERY_TYPE_A); // Type A query
    qinfo->qclass = htons(DNS_QUERY_CLASS_IN); // Internet class

    // Send the query
    if(sendto(s, (char*)buf, sizeof(struct DNSHeader) + (strlen((const char*)qname)+1) + sizeof(struct DNSQuestion), 0, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
        perror("Sendto failed");
        return 1;
    }

    // Receive the response
    int i = sizeof(dest);
    if(recvfrom(s, (char*)buf, 65536, 0, (struct sockaddr*)&dest, (socklen_t*)&i) < 0) {
        perror("Recvfrom failed");
        return 1;
    }

    // Move ahead of the DNS header and the query section
    reader = &buf[sizeof(struct DNSHeader) + (strlen((const char*)qname) + 1) + sizeof(struct DNSQuestion)];

    // Read the answers
    for(i = 0; i < ntohs(dns->ans_count); i++) {
        // Read the name
        reader = reader + 2; // Move past the name
        reader = reader + 10; // Move past the type, class, TTL, and data length fields

        struct sockaddr_in result;
        memcpy(&result.sin_addr.s_addr, reader, sizeof(result.sin_addr.s_addr));
        reader = reader + 4; // Move past the IP address

        // Print the IP address
        printf("%s resolved to %s\n", argv[1], inet_ntoa(result.sin_addr));
    }

    close(s);
    return 0;
}
