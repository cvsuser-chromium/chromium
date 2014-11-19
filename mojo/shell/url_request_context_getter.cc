// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/shell/url_request_context_getter.h"

#include "net/cert/cert_verifier.h"
#include "net/cookies/cookie_monster.h"
#include "net/http/http_cache.h"
#include "net/http/http_network_session.h"
#include "net/http/http_server_properties_impl.h"
#include "net/http/transport_security_state.h"
#include "net/proxy/proxy_service.h"
#include "net/ssl/default_server_bound_cert_store.h"
#include "net/ssl/server_bound_cert_service.h"
#include "net/ssl/ssl_config_service_defaults.h"
#include "net/url_request/file_protocol_handler.h"
#include "net/url_request/static_http_user_agent_settings.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_job_factory_impl.h"

namespace mojo {
namespace shell {

URLRequestContextGetter::URLRequestContextGetter(
    base::FilePath base_path,
    base::SingleThreadTaskRunner* network_task_runner,
    base::SingleThreadTaskRunner* file_task_runner,
    base::MessageLoopProxy* cache_task_runner,
    scoped_ptr<net::NetworkDelegate> network_delegate)
    : base_path_(base_path),
      file_task_runner_(file_task_runner),
      network_task_runner_(network_task_runner),
      cache_task_runner_(cache_task_runner),
      network_delegate_(network_delegate.Pass()),
      net_log_(new net::NetLog()) {
}

URLRequestContextGetter::~URLRequestContextGetter() {
}

net::URLRequestContext* URLRequestContextGetter::GetURLRequestContext() {
  if (!url_request_context_) {
    url_request_context_.reset(new net::URLRequestContext());
    url_request_context_->set_net_log(net_log_.get());
    url_request_context_->set_network_delegate(network_delegate_.get());

    storage_.reset(
        new net::URLRequestContextStorage(url_request_context_.get()));

    storage_->set_cookie_store(new net::CookieMonster(NULL, NULL));
    storage_->set_http_user_agent_settings(
        new net::StaticHttpUserAgentSettings("en-us,en", "Mojo/0.1"));

    storage_->set_proxy_service(net::ProxyService::CreateDirect());
    storage_->set_ssl_config_service(new net::SSLConfigServiceDefaults);
    storage_->set_cert_verifier(net::CertVerifier::CreateDefault());
    storage_->set_transport_security_state(new net::TransportSecurityState());
    storage_->set_server_bound_cert_service(new net::ServerBoundCertService(
        new net::DefaultServerBoundCertStore(NULL), file_task_runner_));
    storage_->set_http_server_properties(
        scoped_ptr<net::HttpServerProperties>(
            new net::HttpServerPropertiesImpl()));
    storage_->set_host_resolver(net::HostResolver::CreateDefaultResolver(
        url_request_context_->net_log()));

    net::HttpNetworkSession::Params network_session_params;
    network_session_params.cert_verifier =
        url_request_context_->cert_verifier();
    network_session_params.transport_security_state =
        url_request_context_->transport_security_state();
    network_session_params.server_bound_cert_service =
        url_request_context_->server_bound_cert_service();
    network_session_params.net_log =
        url_request_context_->net_log();
    network_session_params.proxy_service =
        url_request_context_->proxy_service();
    network_session_params.ssl_config_service =
        url_request_context_->ssl_config_service();
    network_session_params.http_server_properties =
        url_request_context_->http_server_properties();
    network_session_params.host_resolver =
        url_request_context_->host_resolver();

    base::FilePath cache_path = base_path_.Append(FILE_PATH_LITERAL("Cache"));

    net::HttpCache::DefaultBackend* main_backend =
        new net::HttpCache::DefaultBackend(
            net::DISK_CACHE,
            net::CACHE_BACKEND_DEFAULT,
            cache_path,
            0,
            cache_task_runner_.get());

    net::HttpCache* main_cache = new net::HttpCache(
        network_session_params, main_backend);
    storage_->set_http_transaction_factory(main_cache);

    scoped_ptr<net::URLRequestJobFactoryImpl> job_factory(
        new net::URLRequestJobFactoryImpl());
    job_factory->SetProtocolHandler(
        "file",
        new net::FileProtocolHandler(file_task_runner_));
    storage_->set_job_factory(job_factory.release());
  }

  return url_request_context_.get();
}

scoped_refptr<base::SingleThreadTaskRunner>
URLRequestContextGetter::GetNetworkTaskRunner() const {
  return network_task_runner_;
}

}  // namespace shell
}  // namespace mojo
