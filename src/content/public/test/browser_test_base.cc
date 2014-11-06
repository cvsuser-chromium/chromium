// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/test/browser_test_base.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/debug/stack_trace.h"
#include "base/message_loop/message_loop.h"
#include "base/sys_info.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/main_function_params.h"
#include "content/public/test/test_utils.h"
#include "net/base/net_errors.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/compositor/compositor_switches.h"
#include "ui/gl/gl_implementation.h"
#include "ui/gl/gl_switches.h"

#if defined(OS_POSIX)
#include "base/process/process_handle.h"
#endif

#if defined(OS_MACOSX)
#include "base/mac/mac_util.h"
#include "base/power_monitor/power_monitor_device_source.h"
#endif

#if defined(OS_ANDROID)
#include "base/threading/thread_restrictions.h"
#include "content/public/browser/browser_main_runner.h"
#include "content/public/browser/browser_thread.h"
#endif

#if defined(USE_AURA)
#include "content/browser/aura/image_transport_factory.h"
#include "ui/compositor/test/test_context_factory.h"
#endif

namespace content {
namespace {

#if defined(OS_POSIX)
// On SIGTERM (sent by the runner on timeouts), dump a stack trace (to make
// debugging easier) and also exit with a known error code (so that the test
// framework considers this a failure -- http://crbug.com/57578).
// Note: We only want to do this in the browser process, and not forked
// processes. That might lead to hangs because of locks inside tcmalloc or the
// OS. See http://crbug.com/141302.
static int g_browser_process_pid;
static void DumpStackTraceSignalHandler(int signal) {
  if (g_browser_process_pid == base::GetCurrentProcId()) {
    logging::RawLog(logging::LOG_ERROR,
                    "BrowserTestBase signal handler received SIGTERM. "
                    "Backtrace:\n");
    base::debug::StackTrace().Print();
  }
  _exit(128 + signal);
}
#endif  // defined(OS_POSIX)

void RunTaskOnRendererThread(const base::Closure& task,
                             const base::Closure& quit_task) {
  task.Run();
  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE, quit_task);
}

// In many cases it may be not obvious that a test makes a real DNS lookup.
// We generally don't want to rely on external DNS servers for our tests,
// so this host resolver procedure catches external queries and returns a failed
// lookup result.
class LocalHostResolverProc : public net::HostResolverProc {
 public:
  LocalHostResolverProc() : HostResolverProc(NULL) {}

  virtual int Resolve(const std::string& host,
                      net::AddressFamily address_family,
                      net::HostResolverFlags host_resolver_flags,
                      net::AddressList* addrlist,
                      int* os_error) OVERRIDE {
    const char* kLocalHostNames[] = {"localhost", "127.0.0.1", "::1"};
    bool local = false;

    if (host == net::GetHostName()) {
      local = true;
    } else {
      for (size_t i = 0; i < arraysize(kLocalHostNames); i++)
        if (host == kLocalHostNames[i]) {
          local = true;
          break;
        }
    }

    // To avoid depending on external resources and to reduce (if not preclude)
    // network interactions from tests, we simulate failure for non-local DNS
    // queries, rather than perform them.
    // If you really need to make an external DNS query, use
    // net::RuleBasedHostResolverProc and its AllowDirectLookup method.
    if (!local) {
      DVLOG(1) << "To avoid external dependencies, simulating failure for "
          "external DNS lookup of " << host;
      return net::ERR_NOT_IMPLEMENTED;
    }

    return ResolveUsingPrevious(host, address_family, host_resolver_flags,
                                addrlist, os_error);
  }

 private:
  virtual ~LocalHostResolverProc() {}
};

}  // namespace

extern int BrowserMain(const MainFunctionParams&);

BrowserTestBase::BrowserTestBase()
    : allow_test_contexts_(true),
      allow_osmesa_(true) {
#if defined(OS_MACOSX)
  base::mac::SetOverrideAmIBundled(true);
  base::PowerMonitorDeviceSource::AllocateSystemIOPorts();
#endif

#if defined(OS_POSIX)
  handle_sigterm_ = true;
#endif

  embedded_test_server_.reset(new net::test_server::EmbeddedTestServer);
}

BrowserTestBase::~BrowserTestBase() {
#if defined(OS_ANDROID)
  // RemoteTestServer can cause wait on the UI thread.
  base::ThreadRestrictions::ScopedAllowWait allow_wait;
  test_server_.reset(NULL);
#endif
}

void BrowserTestBase::SetUp() {
  CommandLine* command_line = CommandLine::ForCurrentProcess();

  // The tests assume that file:// URIs can freely access other file:// URIs.
  command_line->AppendSwitch(switches::kAllowFileAccessFromFiles);

  command_line->AppendSwitch(switches::kDomAutomationController);

  // It is sometimes useful when looking at browser test failures to know which
  // GPU blacklisting decisions were made.
  command_line->AppendSwitch(switches::kLogGpuControlListDecisions);

#if defined(OS_CHROMEOS)
  // If the test is running on the chromeos envrionment (such as
  // device or vm bots), always use real contexts.
  if (base::SysInfo::IsRunningOnChromeOS())
    allow_test_contexts_ = false;
#endif

#if defined(USE_AURA)
  if (command_line->HasSwitch(switches::kDisableTestCompositor))
    allow_test_contexts_  = false;

  // Use test contexts for browser tests unless they override and force us to
  // use a real context.
  if (allow_test_contexts_) {
    content::ImageTransportFactory::InitializeForUnitTests(
        scoped_ptr<ui::ContextFactory>(new ui::TestContextFactory));
  }
#endif

  // When using real GL contexts, we usually use OSMesa as this works on all
  // bots. The command line can override this behaviour to use a real GPU.
  if (command_line->HasSwitch(switches::kUseGpuInTests))
    allow_osmesa_ = false;

  // Some bots pass this flag when they want to use a real GPU.
  if (command_line->HasSwitch("enable-gpu"))
    allow_osmesa_ = false;

#if defined(OS_MACOSX)
  // On Mac we always use a real GPU.
  allow_osmesa_ = false;
#endif

#if defined(OS_ANDROID)
  // On Android we always use a real GPU.
  allow_osmesa_ = false;
#endif

#if defined(OS_CHROMEOS)
  // If the test is running on the chromeos envrionment (such as
  // device or vm bots), the compositor will use real GL contexts, and
  // we should use real GL bindings with it.
  if (base::SysInfo::IsRunningOnChromeOS())
    allow_osmesa_ = false;
#endif

  if (command_line->HasSwitch(switches::kUseGL)) {
    NOTREACHED() <<
        "kUseGL should not be used with tests. Try kUseGpuInTests instead.";
  }

  if (allow_osmesa_) {
    command_line->AppendSwitchASCII(
        switches::kUseGL, gfx::kGLImplementationOSMesaName);
  }

  scoped_refptr<net::HostResolverProc> local_resolver =
      new LocalHostResolverProc();
  rule_based_resolver_ =
      new net::RuleBasedHostResolverProc(local_resolver.get());
  rule_based_resolver_->AddSimulatedFailure("wpad");
  net::ScopedDefaultHostResolverProc scoped_local_host_resolver_proc(
      rule_based_resolver_.get());

  SetUpInProcessBrowserTestFixture();

  MainFunctionParams params(*command_line);
  params.ui_task =
      new base::Closure(
          base::Bind(&BrowserTestBase::ProxyRunTestOnMainThreadLoop, this));

#if defined(OS_ANDROID)
  BrowserMainRunner::Create()->Initialize(params);
  // We are done running the test by now. During teardown we
  // need to be able to perform IO.
  base::ThreadRestrictions::SetIOAllowed(true);
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(base::IgnoreResult(&base::ThreadRestrictions::SetIOAllowed),
                 true));
#else
  BrowserMain(params);
#endif
  TearDownInProcessBrowserTestFixture();
}

void BrowserTestBase::TearDown() {
}

void BrowserTestBase::ProxyRunTestOnMainThreadLoop() {
#if defined(OS_POSIX)
  if (handle_sigterm_) {
    g_browser_process_pid = base::GetCurrentProcId();
    signal(SIGTERM, DumpStackTraceSignalHandler);
  }
#endif  // defined(OS_POSIX)
  RunTestOnMainThreadLoop();
}

void BrowserTestBase::CreateTestServer(const base::FilePath& test_server_base) {
  CHECK(!test_server_.get());
  test_server_.reset(new net::SpawnedTestServer(
      net::SpawnedTestServer::TYPE_HTTP,
      net::SpawnedTestServer::kLocalhost,
      test_server_base));
}

void BrowserTestBase::PostTaskToInProcessRendererAndWait(
    const base::Closure& task) {
  CHECK(CommandLine::ForCurrentProcess()->HasSwitch(switches::kSingleProcess));

  scoped_refptr<MessageLoopRunner> runner = new MessageLoopRunner;

  base::MessageLoop* renderer_loop =
      RenderProcessHostImpl::GetInProcessRendererThreadForTesting();
  CHECK(renderer_loop);

  renderer_loop->PostTask(
      FROM_HERE,
      base::Bind(&RunTaskOnRendererThread, task, runner->QuitClosure()));
  runner->Run();
}

}  // namespace content
