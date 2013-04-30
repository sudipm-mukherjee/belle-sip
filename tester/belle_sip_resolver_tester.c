/*
	belle-sip - SIP (RFC3261) library.
    Copyright (C) 2010-2013  Belledonne Communications SARL

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include "CUnit/Basic.h"
#include "belle-sip/belle-sip.h"
#include "belle_sip_internal.h"
#include "belle_sip_tester.h"
#include "dns.h"


#define IPV4_SIP_DOMAIN		"sip.linphone.org"
#define IPV4_SIP_IP		"37.59.129.73"
#define IPV4_SIP_BAD_DOMAIN	"dummy.linphone.org"
#define IPV4_MULTIRES_DOMAIN	"google.fr"
#define IPV6_SIP_DOMAIN		"videolan.org"
#define IPV6_SIP_IP		"2a01:e0d:1:3:58bf:fa02:0:1"
#define SRV_DOMAIN		"ekiga.net"
#define SIP_PORT		5060


typedef struct endpoint {
	belle_sip_stack_t* stack;
	long unsigned int resolver_id;
	enum dns_type query_type;
	int resolve_done;
	int resolve_ko;
	belle_sip_list_t *results_list; /**< List of struct addrinfo pointers */
} endpoint_t;

static unsigned int  wait_for(belle_sip_stack_t *stack, int *current_value, int expected_value, int timeout) {
#define ITER 100
	uint64_t begin, end;
	begin = belle_sip_time_ms();
	end = begin + timeout;
	while ((*current_value != expected_value) && (belle_sip_time_ms() < end)) {
		if (stack) belle_sip_stack_sleep(stack, ITER);
	}
	if (*current_value != expected_value) return FALSE;
	else return TRUE;
}

static endpoint_t* create_endpoint(enum dns_type query_type) {
	endpoint_t* endpoint;
	if (belle_sip_init_sockets() < 0) return NULL;
	endpoint = belle_sip_new0(endpoint_t);
	endpoint->stack = belle_sip_stack_new(NULL);
	endpoint->query_type = query_type;
	return endpoint;
}

static void free_ai_result(void *ptr) {
	struct addrinfo *ai = (struct addrinfo *)ptr;
	freeaddrinfo(ai);
}

static void free_srv_result(void *ptr) {
	struct dns_srv *srv = (struct dns_srv *)ptr;
	belle_sip_free(srv);
}

static void reset_endpoint(endpoint_t *endpoint) {
	endpoint->resolver_id = 0;
	endpoint->resolve_done = 0;
	endpoint->resolve_ko = 0;
	if ((endpoint->query_type == DNS_T_A) || (endpoint->query_type == DNS_T_AAAA)) {
		belle_sip_list_for_each(endpoint->results_list, free_ai_result);
	} else if (endpoint->query_type == DNS_T_SRV) {
		belle_sip_list_for_each(endpoint->results_list, free_srv_result);
	}
	belle_sip_list_free(endpoint->results_list);
	endpoint->results_list = NULL;
}

static void destroy_endpoint(endpoint_t *endpoint) {
	reset_endpoint(endpoint);
	belle_sip_object_unref(endpoint->stack);
	belle_sip_free(endpoint);
	belle_sip_uninit_sockets();
}

static void resolve_done(void *data, const char *name, belle_sip_list_t *results_list) {
	endpoint_t *client = (endpoint_t *)data;
	BELLESIP_UNUSED(name);
	client->resolve_done = 1;
	if (results_list && (belle_sip_list_size(results_list) > 0)) {
		client->results_list = results_list;
		client->resolve_done = 1;
	} else
		client->resolve_ko = 1;
}

/* Successful IPv4 A query */
static void ipv4_a_query(void) {
	struct addrinfo *ai;
	int timeout;
	endpoint_t *client = create_endpoint(DNS_T_A);

	CU_ASSERT_PTR_NOT_NULL_FATAL(client);
	timeout = belle_sip_stack_get_dns_timeout(client->stack);
	client->resolver_id = belle_sip_resolve(client->stack, IPV4_SIP_DOMAIN, SIP_PORT, AF_INET, resolve_done, client, belle_sip_stack_get_main_loop(client->stack));
	CU_ASSERT_NOT_EQUAL(client->resolver_id, 0);
	CU_ASSERT_TRUE(wait_for(client->stack, &client->resolve_done, 1, timeout));
	CU_ASSERT_PTR_NOT_EQUAL(client->results_list, NULL);
	if (client->results_list && (belle_sip_list_size(client->results_list) > 0)) {
		struct addrinfo *result_ai = belle_sip_list_nth_data(client->results_list, 0);
		struct sockaddr_in *sock_in = (struct sockaddr_in *)result_ai->ai_addr;
		CU_ASSERT_EQUAL(ntohs(sock_in->sin_port), SIP_PORT);
		ai = belle_sip_ip_address_to_addrinfo(AF_INET, IPV4_SIP_IP, SIP_PORT);
		if (ai) {
			CU_ASSERT_EQUAL(sock_in->sin_addr.s_addr, ((struct sockaddr_in *)ai->ai_addr)->sin_addr.s_addr);
			freeaddrinfo(ai);
		}
	}

	destroy_endpoint(client);
}

/* Successful IPv4 A query with no result */
static void ipv4_a_query_no_result(void) {
	int timeout;
	endpoint_t *client = create_endpoint(DNS_T_A);

	CU_ASSERT_PTR_NOT_NULL_FATAL(client);
	timeout = belle_sip_stack_get_dns_timeout(client->stack);
	client->resolver_id = belle_sip_resolve(client->stack, IPV4_SIP_BAD_DOMAIN, SIP_PORT, AF_INET, resolve_done, client, belle_sip_stack_get_main_loop(client->stack));
	CU_ASSERT_NOT_EQUAL(client->resolver_id, 0);
	CU_ASSERT_TRUE(wait_for(client->stack, &client->resolve_done, 1, timeout));
	CU_ASSERT_PTR_EQUAL(client->results_list, NULL);

	destroy_endpoint(client);
}

/* IPv4 A query send failure */
static void ipv4_a_query_send_failure(void) {
	endpoint_t *client = create_endpoint(DNS_T_A);

	CU_ASSERT_PTR_NOT_NULL_FATAL(client);
	belle_sip_stack_set_resolver_send_error(client->stack, -1);
	client->resolver_id = belle_sip_resolve(client->stack, IPV4_SIP_DOMAIN, SIP_PORT, AF_INET, resolve_done, client, belle_sip_stack_get_main_loop(client->stack));
	CU_ASSERT_EQUAL(client->resolver_id, 0);
	belle_sip_stack_set_resolver_send_error(client->stack, 0);

	destroy_endpoint(client);
}

/* IPv4 A query timeout */
static void ipv4_a_query_timeout(void) {

	endpoint_t *client = create_endpoint(DNS_T_A);

	CU_ASSERT_PTR_NOT_NULL_FATAL(client);
	belle_sip_stack_set_dns_timeout(client->stack, 0);
	client->resolver_id = belle_sip_resolve(client->stack, "toto.com", SIP_PORT, AF_INET, resolve_done, client, belle_sip_stack_get_main_loop(client->stack));
	CU_ASSERT_NOT_EQUAL(client->resolver_id, 0);
	CU_ASSERT_TRUE(wait_for(client->stack, &client->resolve_done, 1, 200));
	CU_ASSERT_PTR_EQUAL(client->results_list, NULL);
	CU_ASSERT_EQUAL(client->resolve_ko,1);
	destroy_endpoint(client);
}

/* Successful IPv4 A query with multiple results */
static void ipv4_a_query_multiple_results(void) {
	int timeout;
	endpoint_t *client = create_endpoint(DNS_T_A);

	CU_ASSERT_PTR_NOT_NULL_FATAL(client);
	timeout = belle_sip_stack_get_dns_timeout(client->stack);
	client->resolver_id = belle_sip_resolve(client->stack, IPV4_MULTIRES_DOMAIN, SIP_PORT, AF_INET, resolve_done, client, belle_sip_stack_get_main_loop(client->stack));
	CU_ASSERT_NOT_EQUAL(client->resolver_id, 0);
	CU_ASSERT_TRUE(wait_for(client->stack, &client->resolve_done, 1, timeout));
	CU_ASSERT_PTR_NOT_EQUAL(client->results_list, NULL);
	if (client->results_list) {
		CU_ASSERT_NOT_EQUAL(belle_sip_list_size(client->results_list), 0);
		CU_ASSERT_NOT_EQUAL(belle_sip_list_size(client->results_list), 1);
	}

	destroy_endpoint(client);
}

/* Successful IPv6 AAAA query */
static void ipv6_aaaa_query(void) {
	struct addrinfo *ai;
	int timeout;
	endpoint_t *client = create_endpoint(DNS_T_AAAA);

	CU_ASSERT_PTR_NOT_NULL_FATAL(client);
	timeout = belle_sip_stack_get_dns_timeout(client->stack);
	client->resolver_id = belle_sip_resolve(client->stack, IPV6_SIP_DOMAIN, SIP_PORT, AF_INET6, resolve_done, client, belle_sip_stack_get_main_loop(client->stack));
	CU_ASSERT_NOT_EQUAL(client->resolver_id, 0);
	CU_ASSERT_TRUE(wait_for(client->stack, &client->resolve_done, 1, timeout));
	CU_ASSERT_PTR_NOT_EQUAL(client->results_list, NULL);
	if (client->results_list && (belle_sip_list_size(client->results_list) > 0)) {
		struct addrinfo *result_ai = belle_sip_list_nth_data(client->results_list, 0);
		struct sockaddr_in6 *sock_in6 = (struct sockaddr_in6 *)result_ai->ai_addr;
		CU_ASSERT_EQUAL(ntohs(sock_in6->sin6_port), SIP_PORT);
		ai = belle_sip_ip_address_to_addrinfo(AF_INET6, IPV6_SIP_IP, SIP_PORT);
		if (ai) {
			struct in6_addr *ipv6_address = &((struct sockaddr_in6 *)ai->ai_addr)->sin6_addr;
			int i;
			for (i = 0; i < 8; i++) {
				CU_ASSERT_EQUAL(sock_in6->sin6_addr.s6_addr[i], ipv6_address->s6_addr[i]);
			}
			freeaddrinfo(ai);
		}
	}

	destroy_endpoint(client);
}

/* Successful SRV query */
static void srv_query(void) {
	int timeout;
	endpoint_t *client = create_endpoint(DNS_T_SRV);

	CU_ASSERT_PTR_NOT_NULL_FATAL(client);
	timeout = belle_sip_stack_get_dns_timeout(client->stack);
	client->resolver_id = belle_sip_resolve_srv(client->stack, SRV_DOMAIN, "udp", resolve_done, client, belle_sip_stack_get_main_loop(client->stack));
	CU_ASSERT_NOT_EQUAL(client->resolver_id, 0);
	CU_ASSERT_TRUE(wait_for(client->stack, &client->resolve_done, 1, timeout));
	CU_ASSERT_PTR_NOT_EQUAL(client->results_list, NULL);
	CU_ASSERT_NOT_EQUAL(belle_sip_list_size(client->results_list), 0);
	if (client->results_list && (belle_sip_list_size(client->results_list) > 0)) {
		struct dns_srv *result_srv = belle_sip_list_nth_data(client->results_list, 0);
		CU_ASSERT_EQUAL(result_srv->port, SIP_PORT);
	}

	destroy_endpoint(client);
}


test_t resolver_tests[] = {
	{ "A query (IPv4)", ipv4_a_query },
	{ "A query (IPv4) with no result", ipv4_a_query_no_result },
	{ "A query (IPv4) with send failure", ipv4_a_query_send_failure },
	{ "A query (IPv4) with timeout", ipv4_a_query_timeout },
	{ "A query (IPv4) with multiple results", ipv4_a_query_multiple_results },
	{ "AAAA query (IPv6)", ipv6_aaaa_query },
	{ "SRV query", srv_query },
};

test_suite_t resolver_test_suite = {
	"Resolver",
	NULL,
	NULL,
	sizeof(resolver_tests) / sizeof(resolver_tests[0]),
	resolver_tests
};

