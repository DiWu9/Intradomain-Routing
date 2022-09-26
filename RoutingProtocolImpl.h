#ifndef ROUTINGPROTOCOLIMPL_H
#define ROUTINGPROTOCOLIMPL_H

#include "RoutingProtocol.h"
#include "Node.h"
#define PPSIZE 12
#define DVALARM 30000
#define LSALARM 30000
#define PINGALARM 10000
#define CHECKALARM 1000
#define PONGREFRESH 15000
#define DVREFRESH 45000

#include <unordered_map>
#include <vector>

struct port_info {
  unsigned short link_cost;
  unsigned short neighbor_id;
  unsigned int last_update;
};

struct dv_info {
  unsigned short link_cost;
  unsigned short next_hop;
  unsigned int last_update;
};

struct path_info {
  unsigned int cost;
  unsigned short prev_router;
  unsigned int last_update;
};

class RoutingProtocolImpl : public RoutingProtocol {
  public:
    RoutingProtocolImpl(Node *n);
    ~RoutingProtocolImpl();

    void init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type);
    // As discussed in the assignment document, your RoutingProtocolImpl is
    // first initialized with the total number of ports on the router,
    // the router's ID, and the protocol type (P_DV or P_LS) that
    // should be used. See global.h for definitions of constants P_DV
    // and P_LS.

    void handle_alarm(void *data);
    // As discussed in the assignment document, when an alarm scheduled by your
    // RoutingProtoclImpl fires, your RoutingProtocolImpl's
    // handle_alarm() function will be called, with the original piece
    // of "data" memory supplied to set_alarm() provided. After you
    // handle an alarm, the memory pointed to by "data" is under your
    // ownership and you should free it if appropriate.

    void recv(unsigned short port, void *packet, unsigned short size);
    // When a packet is received, your recv() function will be called
    // with the port number on which the packet arrives from, the
    // pointer to the packet memory, and the size of the packet in
    // bytes. When you receive a packet, the packet memory is under
    // your ownership and you should free it if appropriate. When a
    // DATA packet is created at a router by the simulator, your
    // recv() function will be called for such DATA packet, but with a
    // special port number of SPECIAL_PORT (see global.h) to indicate
    // that the packet is generated locally and not received from 
    // a neighbor router.

    /* functions */
    // ping-pong functions
    void send_ping(unsigned short port);
    void recv_ping_send_pong(unsigned short port, char *packet, unsigned short size);
    void recv_pong(unsigned short port, char *packet);
    void recv_data(unsigned short port, char *packet, unsigned short size);
    bool port_expiration_check(); // remove port that has not been refreshed for 15 seconds

    // dv functions
    void send_dv(unsigned short port, unsigned short dest_id);
    void recv_dv(char *packet);

    // linked-state functions
    bool ls_expiration_check(); // (1/sec) remove the LS entry that is not refreshed within 45 seconds
    bool is_network_topology_changed(unsigned short from_router_id, unordered_map<unsigned short, unsigned int> from_router_nbr); // check if the ls packet will modify the topology
    void send_ls_packet(); // send port info to its neighbors
    void recv_ls_packet(unsigned short port, void *packet); // recv port info and keeps flooding
    void ls_update_shortest_path(); // (1/30sec) find the shortest path to remaining routers using dijkstra algorithm
    // find the port to forward the packet of destination router_id based on the result of dijkstra
    unsigned short find_next_hop(unordered_map<unsigned short, tuple<unsigned int, unsigned short>> distance_map, unsigned short dest_router_id);

 private:

    // debugging tools
    void print_dv_table();
    void print_port_status();
    void print_network_topology();

    /* attributes */
    Node *sys; // To store Node object; used to access GSR9999 interfaces 

    // common attributes
    unsigned short num_ports;
    unsigned short router_id;
    eProtocolType protocol_type;
    unordered_map<unsigned short, port_info> port_status; // port ID: port_status
    unordered_map<unsigned short, unsigned short> forwarding_table; // dest: nexthop router

    // dv attributes
    unordered_map<unsigned short, dv_info> dv_table; // dest ID: dv_info

    // ls attributes
    unordered_map<unsigned short, path_info> ls_shortest_path; // router id -> (min cost, prev router, last refreshed time) map
    unordered_map<unsigned short, unordered_map<unsigned short, unsigned int>> ls_network_topology; // router id -> (nbr id -> nbr cost)
    unordered_map<unsigned short, unsigned int> ls_router_to_seq; // router id -> last sequence number
    unsigned int ls_sequence_number;
    
};

#endif

