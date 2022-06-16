/*
 * Copyright (c) 2020 Roc Streaming authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_pipeline/receiver_session_group.h"
#include "roc_address/socket_addr_to_str.h"
#include "roc_core/log.h"

namespace roc {
namespace pipeline {

ReceiverSessionGroup::ReceiverSessionGroup(
    const ReceiverConfig& receiver_config,
    ReceiverState& receiver_state,
    audio::Mixer& mixer,
    const rtp::FormatMap& format_map,
    packet::PacketFactory& packet_factory,
    core::BufferFactory<uint8_t>& byte_buffer_factory,
    core::BufferFactory<audio::sample_t>& sample_buffer_factory,
    core::IAllocator& allocator)
    : allocator_(allocator)
    , packet_factory_(packet_factory)
    , byte_buffer_factory_(byte_buffer_factory)
    , sample_buffer_factory_(sample_buffer_factory)
    , format_map_(format_map)
    , mixer_(mixer)
    , receiver_state_(receiver_state)
    , receiver_config_(receiver_config) {
}

void ReceiverSessionGroup::route_packet(const packet::PacketPtr& packet) {
    if (packet->rtcp()) {
        route_control_packet_(packet);
        return;
    }

    route_transport_packet_(packet);
}

void ReceiverSessionGroup::update_sessions(packet::timestamp_t timestamp) {
    core::SharedPtr<ReceiverSession> curr, next;

    for (curr = sessions_.front(); curr; curr = next) {
        next = sessions_.nextof(*curr);

        if (!curr->update(timestamp)) {
            remove_session_(*curr);
        }
    }
}

size_t ReceiverSessionGroup::num_sessions() const {
    return sessions_.size();
}

void ReceiverSessionGroup::update_source(packet::source_t ssrc, const char* cname) {
    roc_log(LogDebug, "session group: source updated: ssrc=%lu cname=%s",
            (unsigned long)ssrc, cname);

    // TODO
}

void ReceiverSessionGroup::remove_source(packet::source_t ssrc) {
    roc_log(LogDebug, "session group: source removed: ssrc=%lu", (unsigned long)ssrc);

    // TODO
}

size_t ReceiverSessionGroup::num_receipted_sources() {
    // TODO

    return 0;
}

rtcp::ReceptionMetrics ReceiverSessionGroup::get_reception_metrics(size_t source_index) {
    // TODO

    (void)source_index;

    rtcp::ReceptionMetrics metrics;
    return metrics;
}

void ReceiverSessionGroup::add_sending_metrics(const rtcp::SendingMetrics& metrics) {
    roc_log(LogDebug, "session group: sending metrics: ntp=%llu rtp=%llu",
            (unsigned long long)metrics.origin_ntp,
            (unsigned long long)metrics.origin_rtp);

    // TODO
}

void ReceiverSessionGroup::add_link_metrics(const rtcp::LinkMetrics& metrics) {
    // TODO

    (void)metrics;
}

void ReceiverSessionGroup::route_transport_packet_(const packet::PacketPtr& packet) {
    core::SharedPtr<ReceiverSession> sess;

    for (sess = sessions_.front(); sess; sess = sessions_.nextof(*sess)) {
        if (sess->handle(packet)) {
            return;
        }
    }

    if (can_create_session_(packet)) {
        create_session_(packet);
    }
}

void ReceiverSessionGroup::route_control_packet_(const packet::PacketPtr& packet) {
    if (!rtcp_composer_) {
        rtcp_composer_.reset(new (rtcp_composer_) rtcp::Composer());
    }

    if (!rtcp_session_) {
        rtcp_session_.reset(new (rtcp_session_) rtcp::Session(
            this, NULL, NULL, *rtcp_composer_, packet_factory_, byte_buffer_factory_));
    }

    if (!rtcp_session_->valid()) {
        return;
    }

    // This will invoke IReceiverController methods implemented by us.
    rtcp_session_->process_packet(packet);
}

bool ReceiverSessionGroup::can_create_session_(const packet::PacketPtr& packet) {
    if (packet->flags() & packet::Packet::FlagRepair) {
        roc_log(LogDebug, "session group: ignoring repair packet for unknown session");
        return false;
    }

    return true;
}

void ReceiverSessionGroup::create_session_(const packet::PacketPtr& packet) {
    if (!packet->udp()) {
        roc_log(LogError,
                "session group: can't create session, unexpected non-udp packet");
        return;
    }

    if (!packet->rtp()) {
        roc_log(LogError,
                "session group: can't create session, unexpected non-rtp packet");
        return;
    }

    const ReceiverSessionConfig sess_config = make_session_config_(packet);

    const address::SocketAddr src_address = packet->udp()->src_addr;
    const address::SocketAddr dst_address = packet->udp()->dst_addr;

    roc_log(LogInfo, "session group: creating session: src_addr=%s dst_addr=%s",
            address::socket_addr_to_str(src_address).c_str(),
            address::socket_addr_to_str(dst_address).c_str());

    core::SharedPtr<ReceiverSession> sess = new (allocator_) ReceiverSession(
        sess_config, receiver_config_.common, src_address, format_map_, packet_factory_,
        byte_buffer_factory_, sample_buffer_factory_, allocator_);

    if (!sess || !sess->valid()) {
        roc_log(LogError, "session group: can't create session, initialization failed");
        return;
    }

    if (!sess->handle(packet)) {
        roc_log(LogError,
                "session group: can't create session, can't handle first packet");
        return;
    }

    mixer_.add_input(sess->reader());
    sessions_.push_back(*sess);

    receiver_state_.add_sessions(+1);
}

void ReceiverSessionGroup::remove_session_(ReceiverSession& sess) {
    roc_log(LogInfo, "session group: removing session");

    mixer_.remove_input(sess.reader());
    sessions_.remove(sess);

    receiver_state_.add_sessions(-1);
}

ReceiverSessionConfig
ReceiverSessionGroup::make_session_config_(const packet::PacketPtr& packet) const {
    ReceiverSessionConfig config = receiver_config_.default_session;

    packet::RTP* rtp = packet->rtp();
    if (rtp) {
        config.payload_type = rtp->payload_type;
    }

    packet::FEC* fec = packet->fec();
    if (fec) {
        config.fec_decoder.scheme = fec->fec_scheme;
    }

    return config;
}

} // namespace pipeline
} // namespace roc
