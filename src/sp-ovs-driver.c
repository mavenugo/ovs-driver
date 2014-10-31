
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "lib/vconn.h"
#include "lib/ofp-msgs.h"
#include "lib/ofpbuf.h"
#include "lib/ofp-util.h"
#include "include/openflow/openflow-common.h"
#include "include/ovs-of-driver.h"

#define IPTOS_PREC_INTERNETCONTROL 0xc0
#define DSCP_DEFAULT (IPTOS_PREC_INTERNETCONTROL >> 2)
static enum ofputil_protocol allowed_protocols = OFPUTIL_P_ANY;
int verbosity = 0;

int open_vconn_socket(const char *name, struct vconn **vconnp)
{
    char *vconn_name;
    int error;
    char *bridge_path;
    const char *suffix = "mgmt";

    bridge_path = xasprintf("%s/%s.%s", ovs_rundir(), name, suffix);
    vconn_name = xasprintf("unix:%s", bridge_path);

    printf("VConn : %s", vconn_name);
    error = vconn_open(vconn_name, get_allowed_ofp_versions(), DSCP_DEFAULT,
                       vconnp);
    if (error && error != ENOENT) {
        printf("%s: failed to open socket (%s)", name, ovs_strerror(error));
    }
    vconn_set_recv_any_version(*vconnp);
    error = vconn_connect_block(*vconnp);
    if (error) {
        printf("%s: failed to connect to socket (%s)", name, ovs_strerror(error));
    }

    free(vconn_name);

    return error;
}

char* show_ofctl (char* vconn_name) {
    enum ofp_version version;
    struct vconn *vconn;
    struct ofpbuf *request;
    struct ofpbuf *reply;
    bool has_ports;
    char* ret;
    int error = open_vconn_socket(vconn_name, &vconn);
    if (error) return NULL;

    version = vconn_get_version(vconn);
    request = ofpraw_alloc(OFPRAW_OFPT_FEATURES_REQUEST, version, 0);
    vconn_transact(vconn, request, &reply);

    has_ports = ofputil_switch_features_has_ports(reply);
    if (!has_ports) {
        request = ofputil_encode_port_desc_stats_request(version, OFPP_ANY);
    }

    ret = ofp_to_string(ofpbuf_data(reply), ofpbuf_size(reply), verbosity + 1);
    ofpbuf_delete(reply);
    return ret;
}

int show(char *argv[])
{
    const char *vconn_name = argv[1];
    enum ofp_version version;
    struct vconn *vconn;
    struct ofpbuf *request;
    struct ofpbuf *reply;
    bool has_ports;
    int error = open_vconn_socket(vconn_name, &vconn);
    if (error) return error;

    version = vconn_get_version(vconn);
    request = ofpraw_alloc(OFPRAW_OFPT_FEATURES_REQUEST, version, 0);
    vconn_transact(vconn, request, &reply);

    has_ports = ofputil_switch_features_has_ports(reply);
    if (!has_ports) {
        request = ofputil_encode_port_desc_stats_request(version, OFPP_ANY);
    }

    ofp_print(stdout, ofpbuf_data(reply), ofpbuf_size(reply), verbosity + 1);
    ofpbuf_delete(reply);
    vconn_close(vconn);
    return 0;
}

void
transact_multiple_noreply(struct vconn *vconn, struct list *requests)
{
    struct ofpbuf *request, *reply;

    LIST_FOR_EACH (request, list_node, requests) {
        ofpmsg_update_length(request);
    }

    vconn_transact_multiple_noreply(vconn, requests, &reply);
    if (reply) {
        ofp_print(stderr, ofpbuf_data(reply), ofpbuf_size(reply), verbosity + 2);
        exit(1);
    }
    ofpbuf_delete(reply);
}

void
transact_noreply(struct vconn *vconn, struct ofpbuf *request)
{
    struct list requests;

    list_init(&requests);
    list_push_back(&requests, &request->list_node);
    transact_multiple_noreply(vconn, &requests);
}

bool
try_set_protocol(struct vconn *vconn, enum ofputil_protocol want,
                 enum ofputil_protocol *cur)
{
    for (;;) {
        struct ofpbuf *request, *reply;
        enum ofputil_protocol next;

        request = ofputil_encode_set_protocol(*cur, want, &next);
        if (!request) {
            return *cur == want;
        }

        vconn_transact_noreply(vconn, request, &reply);
        if (reply) {
            char *s = ofp_to_string(ofpbuf_data(reply), ofpbuf_size(reply), 2);
            printf("%s: failed to set protocol, switch replied: %s",
                     vconn_get_name(vconn), s);
            free(s);
            ofpbuf_delete(reply);
            return false;
        }
        *cur = next;
    }
}

enum ofputil_protocol
open_vconn_for_flow_mod(const char *remote, struct vconn **vconnp,
                        enum ofputil_protocol usable_protocols)
{
    enum ofputil_protocol cur_protocol;
    char *usable_s;
    int i;

    if (!(usable_protocols & allowed_protocols)) {
        char *allowed_s = ofputil_protocols_to_string(allowed_protocols);
        usable_s = ofputil_protocols_to_string(usable_protocols);
        ovs_fatal(0, "none of the usable flow formats (%s) is among the "
                  "allowed flow formats (%s)", usable_s, allowed_s);
    }

    /* If the initial flow format is allowed and usable, keep it. */
    cur_protocol = open_vconn_socket(remote, vconnp);
    if (usable_protocols & allowed_protocols & cur_protocol) {
        return cur_protocol;
    }

    /* Otherwise try each flow format in turn. */
    for (i = 0; i < sizeof(enum ofputil_protocol) * CHAR_BIT; i++) {
        enum ofputil_protocol f = 1 << i;

        if (f != cur_protocol
            && f & usable_protocols & allowed_protocols
            && try_set_protocol(*vconnp, f, &cur_protocol)) {
            return f;
        }
    }

    usable_s = ofputil_protocols_to_string(usable_protocols);
    ovs_fatal(0, "switch does not support any of the usable flow "
              "formats (%s)", usable_s);
}

void
ofctl_flow_mod__(const char *remote, struct ofputil_flow_mod *fms,
                 size_t n_fms, enum ofputil_protocol usable_protocols)
{
    enum ofputil_protocol protocol;
    struct vconn *vconn;
    size_t i;

    protocol = open_vconn_for_flow_mod(remote, &vconn, usable_protocols);

    for (i = 0; i < n_fms; i++) {
        struct ofputil_flow_mod *fm = &fms[i];

        transact_noreply(vconn, ofputil_encode_flow_mod(fm, protocol));
        free(CONST_CAST(struct ofpact *, fm->ofpacts));
    }
    vconn_close(vconn);
}

void
ofctl_flow_mod(int argc, char *argv[], uint16_t command)
{
    struct ofputil_flow_mod fm;
    char *error;
    enum ofputil_protocol usable_protocols;

    error = parse_ofp_flow_mod_str(&fm, argc > 2 ? argv[2] : "", command,
                                   &usable_protocols);
    if (error) {
        printf("%s", error);
    }
    ofctl_flow_mod__(argv[1], &fm, 1, usable_protocols);
}

void
add_flow(int argc, char *argv[])
{
    ofctl_flow_mod(argc, argv, OFPFC_ADD);
}

/*
void
execute_command(int argc, char *argv[]) {
    int error = 0;
    if (!strcmp("show", argv[0])) {
        error = show(argv);
    } else if (!strcmp("add", argv[0])) {
        add_flow(argc, argv);
    }

    printf("error = %d", error);
    if (error != ENOENT) {
        printf("(%s)\n", ovs_strerror(error));
    } 
    printf("\n");
    if (error) exit(1);
}

void main (int argc, char *argv[]) {
    execute_command(argc - 1, argv + 1);
}
*/
