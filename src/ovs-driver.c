
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "third-party/ovs/lib/vconn.h"
#include "third-party/ovs/lib/ofp-msgs.h"
#include "third-party/ovs/lib/ofpbuf.h"
#include "third-party/ovs/lib/ofp-util.h"
#include "third-party/ovs/include/openflow/openflow-common.h"
#include "include/ovs-driver.h"

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

    error = vconn_open(vconn_name, get_allowed_ofp_versions(), DSCP_DEFAULT,
                       vconnp);
    if (error && error != ENOENT) {
        printf("%s: failed to open socket (%s)", name, ovs_strerror(error));
    }
    error = vconn_connect_block(*vconnp);
    if (error) {
        printf("%s: failed to connect to socket (%s)", name, ovs_strerror(error));
    }

    free(vconn_name);

    return error;
}

char* ovs_get_features (char* vconn_name) {
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
    int ofp_version;
    int error;

    if (!(usable_protocols & allowed_protocols)) {
        char *allowed_s = ofputil_protocols_to_string(allowed_protocols);
        usable_s = ofputil_protocols_to_string(usable_protocols);
        ovs_fatal(0, "none of the usable flow formats (%s) is among the "
                  "allowed flow formats (%s)", usable_s, allowed_s);
    }

    /* If the initial flow format is allowed and usable, keep it. */
    error = open_vconn_socket(remote, vconnp);
    if (error) {
        printf("ERROR :-( %d", error);
    }

    ofp_version = vconn_get_version(*vconnp);
    cur_protocol = ofputil_protocol_from_ofp_version(ofp_version);
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
ovs_flow_mod__(const char *remote, struct ofputil_flow_mod *fms,
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
ovs_flow_mod(char* bridge_name, char *flow, uint16_t command_type)
{
    struct ofputil_flow_mod fm;
    char *error;
    enum ofputil_protocol usable_protocols;

    error = parse_ofp_flow_mod_str(&fm, flow, command_type,
                                   &usable_protocols);
    if (error) {
        printf("%s", error);
    }
    ovs_flow_mod__(bridge_name, &fm, 1, usable_protocols);
}

void
ovs_del_flow(char* bridge_name, char *flow)
{
    ovs_flow_mod(bridge_name, flow, OFPFC_DELETE_STRICT);
}

void
ovs_mod_flow(char* bridge_name, char *flow)
{
    ovs_flow_mod(bridge_name, flow, OFPFC_MODIFY_STRICT);
}

void
ovs_add_flow(char* bridge_name, char *flow)
{
    ovs_flow_mod(bridge_name, flow, OFPFC_ADD);
}

void
execute_command(int argc, char *argv[]) {
    int error = 0;
    if (!strcmp("show", argv[0])) {
        error = show(argv);
    } else if (!strcmp("add", argv[0])) {
        ovs_add_flow(argv[1], argv[2]);
    } else if (!strcmp("del", argv[0])) {
        ovs_del_flow(argv[1], argv[2]);
    } else if (!strcmp("mod", argv[0])) {
        ovs_mod_flow(argv[1], argv[2]);
    }

    printf("error = %d", error);
    if (error != ENOENT) {
        printf("(%s)\n", ovs_strerror(error));
    } 
    printf("\n");
    if (error) exit(1);
}
