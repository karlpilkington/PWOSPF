/************************
*
*  HELLO Packet Functions
*
************************/

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"
#include "sr_pwospf.h"
#include "pwospf_protocol.h"
#include "top_info.h"
#include "hello.h"


/*******************************************************************
*   Called when handle_packet() receives a HELLO packet.
********************************************************************/
void handle_HELLO(struct packet_state* ps, struct ip* ip_hdr)
{
    fprintf(stderr, "In handle_hello\n");
	struct ospfv2_hdr* pwospf_hdr = 0;
	struct ospfv2_hello_hdr* hello_hdr = 0;
	
	/*lock pwospf subsys*/
	fprintf(stderr, "Locking in handle_hello()\n");
	pwospf_lock(ps->sr->ospf_subsys);
	struct pwospf_iflist* iface = ps->sr->ospf_subsys->interfaces;

	if(ps->len < (sizeof(struct ospfv2_hdr) + sizeof(struct ospfv2_hello_hdr))) /* incorrectly sized packet */
	{
		fprintf(stderr, "Malformed HELLO Packet.");
		ps->res_len=0;
	}
	else /* correctly sized packet */
	{
		pwospf_hdr = (struct ospfv2_hdr*)(ps->packet);
		hello_hdr = (struct ospfv2_hello_hdr*)(ps->packet + sizeof(struct ospfv2_hdr));

		/* check incoming packet values against ONLY the receiving interface in the interface list */
		if(iface == 0)
		{
			fprintf(stderr, "ERROR - INTERFACE LIST NOT INITIALIZED");
		}
		int found = 0;
		while(iface)
		{
			if(strcmp(iface->name, ps->interface) == 0) /* if the current interface equals the incoming interface */
			{
				
				if((iface->mask.s_addr != ntohl(hello_hdr->nmask)) || (iface->helloint != ntohs(hello_hdr->helloint)))
				{
					/* drop packet */
					fprintf(stderr,"HELLO doesn't match any interface - packet dropped");
					pwospf_unlock(ps->sr->ospf_subsys);
					return;
				}

				/* once interface is decided: */
				struct neighbor_list* neighbor_list_walker = 0;
		
				if(iface->neighbors == 0) /* no neighbors known - add new neighbor */
				{
					iface->neighbors = (struct neighbor_list*) malloc(sizeof(struct neighbor_list));
					assert(iface->neighbors);
					iface->neighbors->next = 0;
					iface->neighbors->id = pwospf_hdr->rid;
					iface->neighbors->ip_address = ip_hdr->ip_src;
					iface->neighbors->timenotvalid = time(NULL) + OSPF_NEIGHBOR_TIMEOUT;
					found = 1;
				}
				else /* add to end of iface->neighbors (end of neighbor_list_walker) */
				{
					neighbor_list_walker = iface->neighbors;
					struct neighbor_list* prev = NULL;
					while(neighbor_list_walker != NULL)
					{
						if(neighbor_list_walker->timenotvalid < time(NULL))
						{
							neighbor_list_walker = delete_neighbor_list(iface, neighbor_list_walker, prev);
						}
						
						if(neighbor_list_walker->ip_address.s_addr == ip_hdr->ip_src.s_addr) /* ?????????? SHOULD THIS BE COMPARING THE rid, NOT THE ip_address? */
						{
							neighbor_list_walker->timenotvalid = time(NULL) + OSPF_NEIGHBOR_TIMEOUT;
							found = 1;
							break;
						}
						
						prev = neighbor_list_walker;
						neighbor_list_walker = neighbor_list_walker->next;
					}
					/* no matching neighbor found - add new neighbor */
					if(found == 0)
					{
                        neighbor_list_walker->next = (struct neighbor_list*) malloc(sizeof(struct neighbor_list));
                        assert(neighbor_list_walker->next);
                        neighbor_list_walker = neighbor_list_walker->next;
                        neighbor_list_walker->next = 0;
                        neighbor_list_walker->id = pwospf_hdr->rid;
                        neighbor_list_walker->ip_address = ip_hdr->ip_src;
                        neighbor_list_walker->timenotvalid = time(NULL) + OSPF_NEIGHBOR_TIMEOUT;
                        found = 1;
				    }
				}
				/*add to list of subnets*/
				
				struct in_addr new_pref;
				new_pref.s_addr = (ip_hdr->ip_src.s_addr & htonl(hello_hdr->nmask));
				struct route* old_sub = router_contains_subnet(ps->sr->ospf_subsys->this_router, new_pref.s_addr);
				/*This is a check for that weird FAQ issue about initialzing subnets*/
				/*Basically, if we initialized the connection at startup, then later received an LSU*/
				if(old_sub != NULL && old_sub->r_id == 0 && old_sub->mask.s_addr == ntohl(hello_hdr->nmask))
				{
						old_sub->r_id = ntohl(pwospf_hdr->rid);
				}
			}
			if(found)
			{
			    break;
			}
			iface = iface->next;
		}
	}
	pwospf_unlock(ps->sr->ospf_subsys);
	return;
}

/*******************************************************************
*   Deletes a neigbor_list_entry from neighbors.
*******************************************************************/
struct neighbor_list* delete_neighbor_list(struct pwospf_iflist* iface, struct neighbor_list* walker, struct neighbor_list* prev)
{
		
	if(prev == 0)          /* Item is first in list. */  
	{
		if(iface->neighbors->next)
		{
			iface->neighbors = iface->neighbors->next;
		}	
		else
		{
			iface->neighbors = NULL;
		}
	}
	else if(!walker->next) /* Item is last in list. */
	{
		prev->next = NULL;
	}
	else                    /* Item is in the middle of list. */
	{
		prev->next = walker->next;
	}
	
	/* Walker is still on item to be deleted so free that item. */
	if(walker)
		free(walker);
		
	/*Return next item in list after deleted item. */
	if(prev != NULL)
		return prev->next;
	return NULL;
}

/*******************************************************************
*   Prints all of the Neighbor Lists for all Interfaces.
*******************************************************************/
void print_all_neighbor_lists(struct packet_state* ps)
{
	struct pwospf_iflist* interface_list_walker = 0;

	printf("--- INTERFACE LIST ---\n");
	if(ps->sr->ospf_subsys->interfaces == 0)
	{
		printf("INTERFACE LIST IS EMPTY\n");
		return;
	}
	interface_list_walker = ps->sr->ospf_subsys->interfaces;
	while(interface_list_walker)
	{
		printf("Interface IP: %i", interface_list_walker->address.s_addr); /* OLD: inet_ntoa(interface_list_walker->ip_add) */
		printf("--- NEIGHBOR LIST ---\n");
		struct neighbor_list* neighbor_list_walker = 0;
		if(interface_list_walker->neighbors == 0)
		{
			printf("NEIGHBOR LIST IS EMPTY\n");
			return;
		}
		neighbor_list_walker = interface_list_walker->neighbors; /* WHY DOES THIS GENERATE A WARNING?? */
		while(neighbor_list_walker)
		{
			print_neighbor_list(neighbor_list_walker);
			neighbor_list_walker = neighbor_list_walker->next;
		}
		interface_list_walker = interface_list_walker->next;
	}
}

/*******************************************************************
*   Prints a single Neighbor List Entry.
*******************************************************************/
void print_neighbor_list(struct neighbor_list* ent)
{
	struct in_addr ip_addr;
	assert(ent);
	ip_addr = ent->ip_address;
	printf("IP: %s\t", inet_ntoa(ip_addr));
	printf("Time when Invalid: %lu\n",(long)ent->timenotvalid);
}

/*******************************************************************
*   Creates and sends a HELLO packet with Ethernet, IP, OSPF, and OSPF_HELLO headers.
*******************************************************************/
void send_HELLO(struct sr_instance* sr)
{
	uint16_t packet_size = sizeof(struct sr_ethernet_hdr) + sizeof(struct ip) + 
	                                    sizeof(struct ospfv2_hdr) + sizeof(struct ospfv2_hello_hdr);
	uint8_t* outgoing_packet_buffer = (uint8_t*)malloc(packet_size);
	
	struct sr_ethernet_hdr* eth_hdr = (struct sr_ethernet_hdr*) outgoing_packet_buffer;
	struct ip* ip_hdr = (struct ip*) (outgoing_packet_buffer + sizeof(struct sr_ethernet_hdr));
	struct ospfv2_hdr* pwospf_hdr = (struct ospfv2_hdr*) (outgoing_packet_buffer + 
	                                    sizeof(struct sr_ethernet_hdr) + sizeof(struct ip));
	struct ospfv2_hello_hdr* hello_hdr = (struct ospfv2_hello_hdr*) (outgoing_packet_buffer + 
	                sizeof(struct sr_ethernet_hdr) + sizeof(struct ip) + sizeof(struct ospfv2_hdr));
	
	/* Set Ethernet destination MAC address to ff:ff:ff:ff:ff:ff (Broadcast) */
	int i = 0;
	for(i=0; i<ETHER_ADDR_LEN; i++)
	{
		eth_hdr->ether_dhost[i] = 0xff;
	}
	eth_hdr->ether_type=htons(ETHERTYPE_IP);

	/* Set IP destination IP address to 224.0.0.5 (0xe0000005) (Broadcast) */
	
	ip_hdr->ip_hl = (sizeof(struct ip))/4;
	fprintf(stderr, "IP header length: %u\n", ip_hdr->ip_hl);
	ip_hdr->ip_v = IP_VERSION;
	ip_hdr->ip_tos=ROUTINE_SERVICE;
	ip_hdr->ip_len = htons(packet_size - sizeof(struct sr_ethernet_hdr));
	ip_hdr->ip_id = 0; 
	ip_hdr->ip_off = 0; 
	ip_hdr->ip_ttl = INIT_TTL;
	ip_hdr->ip_p = OSPFV2_TYPE;
	ip_hdr->ip_dst.s_addr = htonl(OSPF_AllSPFRouters); 

    /* Set up HELLO header. */
	hello_hdr->helloint = htons(OSPF_DEFAULT_HELLOINT);
	hello_hdr->padding = 0;

	/* Set up PWOSPF header. */
	pwospf_hdr->version = OSPF_V2;
	pwospf_hdr->type = OSPF_TYPE_HELLO;
	pwospf_hdr->len = htons(sizeof(struct ospfv2_hdr) + sizeof(struct ospfv2_hello_hdr));
	fprintf(stderr, "Locking in send_hello()\n");
	pwospf_lock(sr->ospf_subsys);
	pwospf_hdr->rid = sr->ospf_subsys->this_router->rid;
	pwospf_hdr->aid = htonl(sr->ospf_subsys->area_id);
	pwospf_hdr->autype = OSPF_DEFAULT_AUTHKEY;
	pwospf_hdr->audata = OSPF_DEFAULT_AUTHKEY;
	
	

	

	/* Send the packet out on each interface. */
	struct pwospf_iflist* iface = sr->ospf_subsys->interfaces;
	assert(iface);
	while(iface)
	{
		
		hello_hdr->nmask = htonl(iface->mask.s_addr);
		pwospf_hdr->csum = 0;
	    pwospf_hdr->csum = cksum((uint8_t *)pwospf_hdr, sizeof(struct ospfv2_hdr)-8); 
	    pwospf_hdr->csum = htons(pwospf_hdr->csum);
		
		
		ip_hdr->ip_src = iface->address;
		ip_hdr->ip_sum = 0;
        ip_hdr->ip_sum = cksum((uint8_t *)ip_hdr, sizeof(struct ip));
        ip_hdr->ip_sum = htons(ip_hdr->ip_sum);
	
	    memmove(eth_hdr->ether_shost, iface->mac, ETHER_ADDR_LEN);
	
		sr_send_packet(sr, outgoing_packet_buffer, packet_size, iface->name);
	    iface = iface->next;
	}

	if(outgoing_packet_buffer)
		free(outgoing_packet_buffer);
	pwospf_unlock(sr->ospf_subsys);
}
