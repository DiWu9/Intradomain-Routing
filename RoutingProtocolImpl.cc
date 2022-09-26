#include "RoutingProtocolImpl.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <climits>
#include <set>

RoutingProtocolImpl::RoutingProtocolImpl(Node *n) : RoutingProtocol(n) {
  sys = n;
  // add your own code
}

RoutingProtocolImpl::~RoutingProtocolImpl() {
  // add your own code (if needed)
}

void RoutingProtocolImpl::init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type) {
  // add your own code
  // cout<< "init function" << endl;
  this->num_ports = num_ports;
  this->router_id = router_id;
  this->protocol_type = protocol_type;
  char* type = new char[4];
  type[0] = 'p';
  type[1] = 'c';
  type[2] = 'd';
  type[3] = 'l';
  // initialize port status
  for (unsigned short i = 0; i < num_ports; i++) {
    this->port_status[i] = {INFINITY_COST, 0, sys->time()};
    send_ping(i);
  } 
  // Set alarm for PING
  sys->set_alarm(this, PINGALARM, (void *)&type[0]);
  
  // Set alarm for checking state's freshness
  sys->set_alarm(this, CHECKALARM, (void *)&type[1]);

  // Set alarm for DV
  if (this->protocol_type == P_DV) {
    sys->set_alarm(this, DVALARM, (void *)&type[2]);
  } 
  else if (this->protocol_type == P_LS) {
    this->ls_sequence_number = 0;
    sys->set_alarm(this, LSALARM, (void *)&type[3]);
  }
}

void RoutingProtocolImpl::handle_alarm(void *data) {
  char* type = new char[4];
  type[0] = 'p';
  type[1] = 'c';
  type[2] = 'd';
  type[3] = 'l';
  // cout<< "handle alarm" << endl;
  char *d = static_cast<char*>(data);
  if (*d == 'p') { // p for ping
    // cout<< "ping alarm" << endl;
    for (unsigned short i = 0; i < this->num_ports; i++) {
      send_ping(i);
    } 
    // set alarm
    sys->set_alarm(this, PINGALARM, (void *)&type[0]);
  } 
  else if (*d == 'd') { // d for dv
    for (auto item: this->port_status) {
      if (item.second.link_cost != INFINITY_COST) {
        // cout<< "dv alarm" << endl;
        send_dv(item.first, item.second.neighbor_id);
      }
    }
    sys->set_alarm(this, DVALARM, (void *)&type[2]);
  } 
  else if (*d == 'l') { // l for ls
    this->send_ls_packet();
    sys->set_alarm(this, LSALARM, (void *)&type[3]);
  } 
  else if (*d == 'c') { // c for check state's freshness
    if (this->protocol_type == P_DV) {
      // cout<< "check alarm" << endl;
      // check port status freshness
      for (unsigned short i = 0; i < this->num_ports; i++) {
        unsigned int time_diff_pong = sys->time() - this->port_status[i].last_update;
        // cout << "port no: " << i << " time_diff_ping: " << time_diff_pong << endl;
        if (time_diff_pong > PONGREFRESH && this->port_status[i].link_cost != INFINITY_COST) {
          // set the port link_cost to infinity to update the port_status while the link is dying
          this->port_status[i].link_cost = INFINITY_COST;
          this->port_status[i].last_update = sys->time();
          // update the dv table
          unsigned short neighbor = this->port_status[i].neighbor_id;
          for (auto item : this->dv_table) {
            if (item.second.next_hop == neighbor) {
              this->dv_table[item.first].link_cost = INFINITY_COST;
              this->dv_table[item.first].last_update = sys->time();
            }
          }
          bool del = true;
          for (auto item: this->port_status) {
            if (item.second.link_cost != INFINITY_COST) {
              del = false;
            }
          }
          if (del) {
            this->dv_table.clear();
          }
          // send dv
          for (auto item: this->port_status) {
            if (item.second.neighbor_id != neighbor) {
              // cout << "link dead" << endl;
              send_dv(item.first, item.second.neighbor_id);
            }
          }
        }
      } 
      // check DV state freshness
      for (auto item : this->dv_table) {
        unsigned int time_diff_dv = sys->time() - item.second.last_update;
        if (time_diff_dv > DVREFRESH) {
          // remove from dv_table
          this->dv_table[item.first].link_cost = INFINITY_COST;
          this->dv_table[item.first].last_update = sys->time();
        }
      }
    }
    else if (this->protocol_type == P_LS) {
      // port status check: if port status changed
      // send updated ls packet to other routers
      if (port_expiration_check()) {
        send_ls_packet();
      }
      // if some elements in shortest path is expired
      if (ls_expiration_check()) {
        ls_update_shortest_path();
      }
    }

    
    sys->set_alarm(this, CHECKALARM, (void *)&type[1]);
  }
}

bool RoutingProtocolImpl :: port_expiration_check() {
  bool is_expired = false;
  for (auto port_it : this->port_status) {
    if (sys->time() - port_it.second.last_update > PONGREFRESH) {
      // set the port link_cost to infinity to update the port_status while the link is dying
      this->port_status[port_it.first].link_cost = INFINITY_COST;
      is_expired = true;
    }
  }
  return is_expired;
}

void RoutingProtocolImpl::recv(unsigned short port, void *packet, unsigned short size) {
  // cout << "Router ID " << this->router_id << " recv function" << endl;
  char type = *(char *)packet;
  if (type == (char)DATA) {
    recv_data(port, (char *)packet, size);
  } else if (type == (char)PING) {
    recv_ping_send_pong(port, (char *)packet, size);
  } else if (type == (char)PONG) {
    recv_pong(port, (char *)packet);
  } else if (type == (char)DV) {
    recv_dv((char *)packet);
  } else if (type == (char)LS) {
    recv_ls_packet(port, (char *)packet);
  }
}

void RoutingProtocolImpl::send_ping(unsigned short port) {
  // cout<< "Router ID " << this->router_id << " send_ping function" << endl;
  char* ping_packet = new char[PPSIZE];
  // packet type
  *ping_packet = (char) PING;
  // packet size in 
  *(unsigned short *)(ping_packet + 2) = (unsigned short)htons(PPSIZE);
  // source ID
  *(unsigned short *)(ping_packet + 4) = (unsigned short)htons(this->router_id);
  // store the current time in the PING message payload
  *(unsigned int *)(ping_packet + 8) = (unsigned int)htonl(sys->time());
  sys->send(port, ping_packet, PPSIZE);
}


void RoutingProtocolImpl::recv_ping_send_pong(unsigned short port, char *packet, unsigned short size) {
  // cout<< "Router ID " << this->router_id << " recv_ping_send_pong function" << endl;
  // send pong message
  char* pong_packet = new char[PPSIZE];
  // packet type
  *pong_packet = (char) PONG;
  // packet size in 
  *(unsigned short *)(pong_packet + 2) = (unsigned short)htons(size);
  // source ID
  *(unsigned short *)(pong_packet + 4) = (unsigned short)htons(this->router_id);
  // dest ID
  *(unsigned short *)(pong_packet + 6) = *(unsigned short *)(packet + 4); 
  // store the current time in the PING message payload
  *(unsigned int *)(pong_packet + 8) = *(unsigned int *)(packet + 8); 
  // send the pong packet
  sys->send(port, pong_packet, PPSIZE);
  free(packet);
}

void RoutingProtocolImpl::recv_pong(unsigned short port, char *packet) {
  // cout<< "Router ID " << this->router_id << " recv_pong function" << endl;
  unsigned short dest_id = (unsigned short)ntohs(*(unsigned short *)(packet + 6));
  if (dest_id != this->router_id) {
    free(packet);
    return;
  }
  unsigned int ping_time = (unsigned int)ntohl(*(unsigned int *)(packet + 8));
  unsigned short rtt = sys->time() - ping_time;
  unsigned short neighbor_id = (unsigned short)ntohs(*(unsigned short *)(packet + 4));

  // check port status change
  bool has_port_info;
  bool has_update;
  if (this->protocol_type == P_LS) {
    has_port_info = this->port_status.find(port) != this->port_status.end();
    has_update = false;
    if (has_port_info) {
      port_info old_info = this->port_status[port];
      has_update = old_info.link_cost != rtt || old_info.neighbor_id != neighbor_id;
    }
  }

  // update port status
  this->port_status[port] = {rtt, neighbor_id, sys->time()};

  // send ls packet if in LS mode and has port status change
  if (this->protocol_type == P_LS && (!has_port_info || has_update)) {
    ls_update_shortest_path();
    send_ls_packet();
  }

  // update dv table
  bool flag = false;
  if (this->protocol_type == P_DV) {
    for (auto item : this->dv_table) {
      this->dv_table[item.first].last_update = sys->time();
    }
    // check if the node exists in dv table
    if (this->dv_table.find(neighbor_id) != this->dv_table.end()) { // key found
      unsigned short c = this->dv_table[neighbor_id].link_cost;
      // update the dv table when either link cost changes
      if (rtt < c) {
        this->dv_table[neighbor_id].link_cost = rtt;
        this->dv_table[neighbor_id].next_hop = neighbor_id;
        flag = true;
      } else if (rtt > c) {
        // neighbor case
        if (this->dv_table[neighbor_id].next_hop == neighbor_id) {
          this->dv_table[neighbor_id].link_cost = rtt;
          flag = true;
        }
      }
      // calculate difference d
      unsigned short d = rtt - c;
      if (d != 0) {
        for (auto item : this->dv_table) {
          if (item.second.next_hop == neighbor_id && item.first != neighbor_id) {
            this->dv_table[item.first].link_cost += d;
            flag = true;
          }
        }
      }
    }
    else { // key not found
      this->dv_table[neighbor_id] = {rtt, neighbor_id, sys->time()};
      // cout<< "key not found" << endl;
      flag = true;
    }
    if (flag) {
      for (auto item: this->port_status) {
        if (item.second.link_cost != INFINITY_COST) {
          // cout << "recvpong" << endl;
          send_dv(item.first, item.second.neighbor_id);
        }
      }
    }
  }
  free(packet);
}


void RoutingProtocolImpl::recv_data(unsigned short port, char *packet, unsigned short size) {

  unsigned short dest_ID = (unsigned short)ntohs(*(unsigned short *)((char *)packet + 6));

  if (this->protocol_type == P_DV) {
    // receive data message
    unsigned short port_id;
    if (dest_ID == this->router_id) {
      free(packet);
      return;
    }
    // send data message
    if (this->dv_table.find(dest_ID) != this->dv_table.end()) { // key found
      for (auto item : port_status) {
        if (item.second.neighbor_id == dest_ID) {
          port_id = item.first;
          break;
        }
      }
      sys->send(port_id, packet, size); 
    } else {
      free(packet);
    }
  }
  else if (this->protocol_type == P_LS) {
    // forward the packet to next hop (until reach destination) using forwarding table
    unsigned short packet_size = (unsigned short)ntohs(*(unsigned short *)((char *)packet + 2));
    if (dest_ID != this->router_id) {
      unsigned short next_hop = this->forwarding_table[dest_ID];
      for (auto port_it : this->port_status) {
        unsigned short port_id = port_it.first;
        if (next_hop == port_it.second.neighbor_id) {
          char * new_data_packet = (char *) malloc(packet_size);
          memcpy(new_data_packet, packet, packet_size);
          sys->send(port_id, (void *) new_data_packet, packet_size);
          free(packet);
          break;
        }
      }
    }
  }
}


void RoutingProtocolImpl::send_dv(unsigned short port, unsigned short dest_id) {
  // cout<< "Router ID " << this->router_id << " send_dv function" << endl;
  if (this->dv_table.size() == 0) {
    return;
  }
  unsigned short DVSIZE = 8 + 4 * this->dv_table.size();
  char* dv_packet = new char[DVSIZE];
  // packet type
  *dv_packet = (char) DV;
  // packet size 
  *(unsigned short *)(dv_packet + 2) = (unsigned short)htons(DVSIZE);
  // source ID
  *(unsigned short *)(dv_packet + 4) = (unsigned short)htons(this->router_id);
  // dest ID
  *(unsigned short *)(dv_packet + 6) = (unsigned short)htons(dest_id);
  // store dv table into the packet
  int i = 8;
  for (auto item : this->dv_table) {
    unsigned short node_id = item.first;
    unsigned short cost = item.second.link_cost;
    unsigned short next_hop = item.second.next_hop;
    // Poison Reverse
    if (next_hop == dest_id)  cost = INFINITY_COST;
    // Node ID
    *(unsigned short *)(dv_packet + i) = (unsigned short)htons(node_id);
    // Cost
    *(unsigned short *)(dv_packet + i + 2) = (unsigned short)htons(cost);
    i += 4;
  }
  // cout << this->router_id << " sending dv to " << dest_id << endl;
  // print_dv_table();
  sys->send(port, dv_packet, DVSIZE);
}


void RoutingProtocolImpl::recv_dv(char *packet) {
  // cout<< "Router ID " << this->router_id << " recv_dv function" << endl;
  unsigned short src_id = (unsigned short)ntohs(*(unsigned short *)(packet + 4));
  unsigned short dest_id = (unsigned short)ntohs(*(unsigned short *)(packet + 6));
  if (dest_id != this->router_id) {
    free(packet);
    return;
  }
  // read packet size
  unsigned short packet_size = (unsigned short)ntohs(*(unsigned short *)(packet + 2));
  unsigned short recv_dvtable_size = (packet_size - 8) / 4;
  unsigned short curr_node, curr_cost, c;
  bool flag = false;
  for (int i = 0; i < recv_dvtable_size; i++) {
    curr_node = (unsigned short)ntohs(*(unsigned short *)(packet + 8 + i * 4));
    curr_cost = (unsigned short)ntohs(*(unsigned short *)(packet + 10 + i * 4));
    // if the dest ID is itself
    if (curr_node == this->router_id) {
      continue;
    }
    // check if the node exists in dv table
    if (this->dv_table.find(curr_node) != this->dv_table.end()) { // key found
      c = this->dv_table[src_id].link_cost;
      // find the min cost
      if (c + curr_cost < this->dv_table[curr_node].link_cost) {
        this->dv_table[curr_node].link_cost = c + curr_cost;
        this->dv_table[curr_node].next_hop = src_id;
        this->dv_table[curr_node].last_update = sys->time();
        flag = true;
      } 
      else if (src_id == this->dv_table[curr_node].next_hop && this->dv_table[curr_node].link_cost != min(curr_cost + c, INFINITY_COST)) {
        this->dv_table[curr_node].link_cost = min(curr_cost + c, INFINITY_COST);
        this->dv_table[curr_node].last_update = sys->time();
        flag = true;
      } 
    }
    else{ // key not found
      unsigned short updated_cost = this->dv_table[src_id].link_cost + curr_cost;
      this->dv_table[curr_node] = {updated_cost, src_id, sys->time()};
      flag = true;
    }
  }
  for (auto item : this->dv_table) {
    this->dv_table[item.first].last_update = sys->time();
  }
  // cout << this->router_id << " recving dv from " << src_id << endl;
  // print_dv_table();
  if (flag) {
    for (auto item: this->port_status) {
      if (item.second.link_cost != INFINITY_COST) {
        send_dv(item.first, item.second.neighbor_id);
      }
    }
  }
  free(packet);
}


void RoutingProtocolImpl::print_dv_table() {
  cout << "----------------------------dv table----------------------------" << endl;
  for (auto item : dv_table) {
    // if (item.second.link_cost != INFINITY_COST) {
      cout << "dest ID: " << item.first << " , linkcost: " << item.second.link_cost << ", next_hop: " << item.second.next_hop << ", last_update: " << item.second.last_update << endl;
    // }
  }
  cout << "----------------------------------------------------------------" << endl;
}


// (1/30sec) find the shortest path to remaining routers using dijkstra algorithm
void RoutingProtocolImpl :: ls_update_shortest_path() {

  //print_port_status();
  //print_network_topology();

  // router id -> (min cost, prev router) map
  unordered_map<unsigned short, tuple<unsigned int, unsigned short>> distance_map;
  // routers that are not visited
  set<unsigned short> unvisited;

  // if v adjacent to A: D(v) = c(A,v)
  unordered_map<unsigned short, port_info>::iterator port_it;
  for (port_it = this->port_status.begin(); port_it != this->port_status.end(); port_it++) {
    port_info port = port_it->second;
    distance_map[port.neighbor_id] = make_tuple(port.link_cost, this->router_id);
  }
  
  // for all routers: if not adj, D(router) = inf
  unordered_map<unsigned short, unordered_map<unsigned short, unsigned int>>::iterator router_it;
  for (router_it = this->ls_network_topology.begin(); router_it != this->ls_network_topology.end(); router_it++) {
    unsigned short router_id = router_it->first;
    if (router_id == this->router_id) {
      continue;
    }
    unvisited.insert(router_id);
    if (distance_map.find(router_id) == distance_map.end()) {
      distance_map[router_id] = make_tuple(INFINITY_COST, this->router_id);
    }
  }
  
  // while there is router unvisited
  unsigned short curr_router;
  unsigned int min_cost;
  while (unvisited.size() > 0) {

    // find w in unvisited such that D(w) is the minimum
    min_cost = INFINITY_COST;
    for (set<unsigned short>::iterator it = unvisited.begin(); it != unvisited.end(); it++) {
      unsigned short unvisited_router = *it;
      unsigned int router_cost = get<0>(distance_map[unvisited_router]);
      if (min_cost > router_cost) {
        curr_router = unvisited_router;
        min_cost = router_cost;
      }
    }
    if (min_cost == INFINITY_COST) {
      break;
    }
    unvisited.erase(curr_router); // remove w from unvisited

    // update D(v) for all v adjacent to w and in unvisited
    unordered_map<unsigned short, unsigned int> curr_router_nbr = this->ls_network_topology[curr_router];
    unordered_map<unsigned short, unsigned int>::iterator nbr_it;
    for (nbr_it = curr_router_nbr.begin(); nbr_it != curr_router_nbr.end(); nbr_it++) {
      unsigned short nbr_router_id = nbr_it->first;
      unsigned int nbr_router_cost = nbr_it->second;
      if (unvisited.find(nbr_router_id) != unvisited.end()) { // update D(v) in unvisited
        if (get<0>(distance_map[nbr_router_id]) > nbr_router_cost + min_cost) {
          distance_map[nbr_router_id] = make_tuple(nbr_router_cost + min_cost, curr_router);
        }
      }
    }
  }

  // update shortest path and forwarding table based on distance map
  unordered_map<unsigned short, tuple<unsigned int, unsigned short>>::iterator dist_it;
  for (dist_it = distance_map.begin(); dist_it != distance_map.end(); dist_it++) {
    unsigned short curr_router_id = dist_it->first;
    tuple<unsigned int, unsigned short> distance = dist_it->second;
    if (get<0>(distance) == INFINITY_COST) { // skip unreachable router
      continue;
    }
    path_info curr_router_info;
    curr_router_info.cost = get<0>(distance);
    curr_router_info.prev_router = get<1>(distance);
    curr_router_info.last_update = sys->time();
    this->ls_shortest_path[curr_router_id] = curr_router_info; // update shortest map
    this->forwarding_table[curr_router_id] = this->find_next_hop(distance_map, curr_router_id); // update forwarding table
  }

  // cout << "Router " << this->router_id << ": ----------- Print forwarding table --------------" << endl;
  // cout << "size: " << this->forwarding_table.size() << endl;
  // for (auto entry_it : this->forwarding_table) {
  //   cout << "Router " << entry_it.first << " goes to " << entry_it.second << "." << endl;
  // }
}

// find the port to forward the packet of destination router_id based on the result of dijkstra
unsigned short RoutingProtocolImpl :: find_next_hop(unordered_map<unsigned short, tuple<unsigned int, unsigned short>> distance_map, unsigned short dest_router_id) {
  unsigned short curr_router_id = dest_router_id;
  // keep finding the prev router until the prev is this router id
  while (get<1>(distance_map[curr_router_id]) != this->router_id) {
    curr_router_id = get<1>(distance_map[curr_router_id]);
  }
  return curr_router_id;
}

/**
 * LS packet format:
 * |0      7|8      15|16            31|
 * |type    |reserved |size            |
 * |source ID         |ignored         |
 * |sequence number                    |
 * |nbr1              |cost1           |
 * |nbr2              |cost2           |
 * ...
 * 
 * send router's port info to its neighbors
 */
void RoutingProtocolImpl :: send_ls_packet() {

  // find active neighbors based on port info collected by ping-pong message
  unordered_map<unsigned short, unsigned short> neighbors;
  for (auto it : this->port_status) {
    if (it.second.link_cost < INFINITY_COST) {
      neighbors[it.second.neighbor_id] = it.second.link_cost;
    }
  }

  // cout << "------------- ls packet send by router " << this->router_id << "---------------" << endl;
  // cout << "size: " << neighbors.size() << endl;
  // for (auto it : neighbors) {
  //   cout << it.first << ": " << it.second << endl;
  // }
  // cout << "---------------------------------------------------------" << endl;
  
  // construct and send ls packet for each nbr
  unsigned short packet_size = 12 + neighbors.size() * 4;
  for (auto port_it : this->port_status) {
    char * packet = (char *) malloc(packet_size);
    *packet = (char) LS; // packet type
    *(unsigned short *)(packet + 2) = (unsigned short)htons(packet_size); // packet size
    *(unsigned short *)(packet + 4) = (unsigned short)htons(this->router_id); // source ID
    *(unsigned int *)(packet + 8) = (unsigned int)(this->ls_sequence_number); // sequence number
    size_t offset = 12;
    for (auto it : neighbors) {
      *(unsigned short *)(packet + offset) = (unsigned short)htons(it.first); // nbr
      *(unsigned short*)(packet + offset + 2) = (unsigned short)htons(it.second); // cost
      offset += 4;
    }
    sys->send(port_it.first, packet, packet_size);
  }
  this->ls_sequence_number++;
}

void RoutingProtocolImpl :: recv_ls_packet(unsigned short port, void *packet) {

  unsigned short from_router = (unsigned short)ntohs(*(unsigned short *)((char *)packet + 4));
  unsigned int sequence_number = (unsigned int)ntohl(*(unsigned int *)((char *)packet + 8));

  // if is a new sequence number
  if (this->ls_router_to_seq.find(from_router) == this->ls_router_to_seq.end() || this->ls_router_to_seq[from_router] < sequence_number) {
    
    // update the most recent sequence number
    this->ls_router_to_seq[from_router] = sequence_number;

    // update the nbr of the from-router in the entire topology
    unordered_map<unsigned short, unsigned int> from_router_nbr;
    unsigned short packet_size = (unsigned short)ntohs(*(unsigned short *)((char *)packet + 2));
    size_t offset = 12;
    while (offset < packet_size) {
      unsigned short nbr_id = (unsigned short)ntohs(*(unsigned short *)((char *)packet + offset));
      unsigned int nbr_cost = (unsigned int)ntohs(*(unsigned short *)((char *)packet + offset + 2)); // not sure type casting works
      from_router_nbr[nbr_id] = nbr_cost;
      offset += 4;
    }

    // cout << "----------------- Print ls packet received by router " << this->router_id << " -----------------" << endl;
    // cout << "Size: " << from_router_nbr.size() << ". Sent by router: " << from_router << "." << endl;
    // for (auto it : from_router_nbr) {
    //   cout << it.first << ": " << it.second << endl;
    // }
    // cout << "--------------------------------------------------------------------------" << endl;

    if (is_network_topology_changed(from_router, from_router_nbr)) {
      cout << "**topology changed**" << endl;
      this->ls_network_topology[from_router] = from_router_nbr;
      ls_update_shortest_path();
    }

    // flood the packet to nbr ports except the receive port
    for (unsigned short flood_port = 0; flood_port < this->num_ports; flood_port++) {
      if (flood_port == port) {
        continue;
      }
      char * ls_packet = (char *) malloc(packet_size);
      memcpy(ls_packet, packet, packet_size);
      sys->send(flood_port, (void *) ls_packet, packet_size);
    }

  }
  free(packet);
}

/**
 * @brief check if the new from router nbr is different from the original in the topology
 * 
 * @param from_router_nbr the new nbr info
 * @return bool 
 */
bool RoutingProtocolImpl :: is_network_topology_changed (unsigned short from_router_id, unordered_map<unsigned short, unsigned int> from_router_nbr) {
  // if doesn't have this router's info
  if (this->ls_network_topology.find(from_router_id) == this->ls_network_topology.end()) {
    return from_router_nbr.size() > 0;
  }
  else {
    unordered_map<unsigned short, unsigned int> original_router_nbr = this->ls_network_topology[from_router_id];
    if (original_router_nbr.size() == from_router_nbr.size()) {
      for (auto nbr_it : from_router_nbr) {
        unsigned short nbr_id = nbr_it.first;
        unsigned int nbr_cost = nbr_it.second;
        if (original_router_nbr.find(nbr_id) == original_router_nbr.end()) { // nbr does not exist
          return true;
        }
        else {
          if (original_router_nbr[nbr_id] != nbr_cost) { // nbr exists but cost is different
            return true;
          }
        }
      }
      return false;
    }
    else {
      return true; // num of nbr is different
    }
  }
}


// (1/sec) remove the LS entry that is not refreshed within 45 seconds
bool RoutingProtocolImpl :: ls_expiration_check() {
  unsigned int curr_time;
  vector<unsigned short> to_remove;
  for (auto path_it : this->ls_shortest_path) {
    curr_time = sys->time();
    if (curr_time - path_it.second.last_update >= 45000) { // if time since last update exceeds 45 seconds
      to_remove.push_back(path_it.first); // add the key (router id) to to_remove
    }
  }
  for (unsigned int router_id : to_remove) { // iteratively remove expired entry by key router id
    this->ls_shortest_path.erase(router_id);
  }
  return to_remove.size() > 0;
  // cout << "Linked state expiration check: " << to_remove.size() << "entry(ies) expired and removed." << endl;
}

void RoutingProtocolImpl :: print_port_status() {
  cout << "Print port status of router " << this->router_id << endl;
  cout << "size: " << this->port_status.size() << endl;
  unordered_map<unsigned short, port_info>::iterator port_it;
  for (port_it = this->port_status.begin(); port_it != this->port_status.end(); port_it++) {
    port_info nbr = port_it->second;
    cout << "Port connects to " << nbr.neighbor_id << " with cost " << nbr.link_cost << endl;
  }
}

void RoutingProtocolImpl :: print_network_topology() {
  cout << "Print network topology of router " << this->router_id << endl;
  cout << "size: " << this->ls_network_topology.size() << endl;
  unordered_map<unsigned short, unordered_map<unsigned short, unsigned int>>::iterator router_it;
  for (router_it = this->ls_network_topology.begin(); router_it != this->ls_network_topology.end(); router_it++) {
    cout << "Router " << router_it->first << " has neighbors: " << endl;
    unordered_map<unsigned short, unsigned int> neighbors = router_it->second;
    for (auto nbr_it : neighbors) {
      cout << "Router id " << nbr_it.first << " of cost " << nbr_it.second << endl;
    }
  }
}