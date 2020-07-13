//---------------------------------------------------------------------------//
// Copyright (c) 2011-2019 Dominik Charousset
// Copyright (c) 2018-2020 Mikhail Komarov <nemo@nil.foundation>
//
// Distributed under the terms and conditions of the BSD 3-Clause License or
// (at your option) under the terms and conditions of the Boost Software
// License 1.0. See accompanying files LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt.
//---------------------------------------------------------------------------//

#include <nil/actor/network/backend/test.hpp>

#include <nil/actor/expected.hpp>
#include <nil/actor/network/actor_proxy_impl.hpp>
#include <nil/actor/network/basp/application.hpp>
#include <nil/actor/network/basp/ec.hpp>
#include <nil/actor/network/make_endpoint_manager.hpp>
#include <nil/actor/network/middleman.hpp>
#include <nil/actor/network/multiplexer.hpp>
#include <nil/actor/network/stream_transport.hpp>
#include <nil/actor/raise_error.hpp>
#include <nil/actor/send.hpp>

namespace nil {
    namespace actor {
        namespace network {
            namespace backend {

                test::test(middleman &mm) : middleman_backend("test"), mm_(mm), proxies_(mm.system(), *this) {
                    // nop
                }

                test::~test() {
                    // nop
                }

                error test::init() {
                    return none;
                }

                void test::stop() {
                    for (const auto &p : peers_)
                        proxies_.erase(p.first);
                    peers_.clear();
                }

                endpoint_manager_ptr test::peer(const node_id &id) {
                    return get_peer(id).second;
                }

                expected<endpoint_manager_ptr> test::get_or_connect(const uri &locator) {
                    if (auto ptr = peer(make_node_id(*locator.authority_only())))
                        return ptr;
                    return make_error(sec::runtime_error, "connecting not implemented in test backend");
                }

                void test::resolve(const uri &locator, const actor &listener) {
                    auto id = locator.authority_only();
                    if (id)
                        peer(make_node_id(*id))->resolve(locator, listener);
                    else
                        anon_send(listener, error(basp::ec::invalid_locator));
                }

                strong_actor_ptr test::make_proxy(node_id nid, actor_id aid) {
                    using impl_type = actor_proxy_impl;
                    using hdl_type = strong_actor_ptr;
                    actor_config cfg;
                    return make_actor<impl_type, hdl_type>(aid, nid, &mm_.system(), cfg, peer(nid));
                }

                void test::set_last_hop(node_id *) {
                    // nop
                }

                std::uint16_t test::port() const noexcept {
                    return 0;
                }

                test::peer_entry &test::emplace(const node_id &peer_id, stream_socket first, stream_socket second) {
                    using transport_type = stream_transport<basp::application>;
                    nonblocking(second, true);
                    auto mpx = mm_.mpx();
                    basp::application app {proxies_};
                    auto mgr = make_endpoint_manager(mpx, mm_.system(), transport_type {second, std::move(app)});
                    if (auto err = mgr->init()) {
                        ACTOR_LOG_ERROR("mgr->init() failed: " << mm_.system().render(err));
                        ACTOR_RAISE_ERROR("mgr->init() failed");
                    }
                    mpx->register_reading(mgr);
                    auto &result = peers_[peer_id];
                    result = std::make_pair(first, std::move(mgr));
                    return result;
                }

                test::peer_entry &test::get_peer(const node_id &id) {
                    auto i = peers_.find(id);
                    if (i != peers_.end())
                        return i->second;
                    auto sockets = make_stream_socket_pair();
                    if (!sockets) {
                        ACTOR_LOG_ERROR("make_stream_socket_pair failed: " << mm_.system().render(sockets.error()));
                        ACTOR_RAISE_ERROR("make_stream_socket_pair failed");
                    }
                    return emplace(id, sockets->first, sockets->second);
                }

            }    // namespace backend
        }        // namespace network
    }            // namespace actor
}    // namespace nil