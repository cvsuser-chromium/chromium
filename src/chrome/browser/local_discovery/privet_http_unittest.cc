// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "chrome/browser/local_discovery/privet_http_impl.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_errors.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::StrictMock;
using testing::NiceMock;

namespace local_discovery {

namespace {

const char kSampleInfoResponse[] = "{"
    "       \"version\": \"1.0\","
    "       \"name\": \"Common printer\","
    "       \"description\": \"Printer connected through Chrome connector\","
    "       \"url\": \"https://www.google.com/cloudprint\","
    "       \"type\": ["
    "               \"printer\""
    "       ],"
    "       \"id\": \"\","
    "       \"device_state\": \"idle\","
    "       \"connection_state\": \"online\","
    "       \"manufacturer\": \"Google\","
    "       \"model\": \"Google Chrome\","
    "       \"serial_number\": \"1111-22222-33333-4444\","
    "       \"firmware\": \"24.0.1312.52\","
    "       \"uptime\": 600,"
    "       \"setup_url\": \"http://support.google.com/\","
    "       \"support_url\": \"http://support.google.com/cloudprint/?hl=en\","
    "       \"update_url\": \"http://support.google.com/cloudprint/?hl=en\","
    "       \"x-privet-token\": \"SampleTokenForTesting\","
    "       \"api\": ["
    "               \"/privet/accesstoken\","
    "               \"/privet/capabilities\","
    "               \"/privet/printer/submitdoc\","
    "       ]"
    "}";

const char kSampleInfoResponseRegistered[] = "{"
    "       \"version\": \"1.0\","
    "       \"name\": \"Common printer\","
    "       \"description\": \"Printer connected through Chrome connector\","
    "       \"url\": \"https://www.google.com/cloudprint\","
    "       \"type\": ["
    "               \"printer\""
    "       ],"
    "       \"id\": \"MyDeviceID\","
    "       \"device_state\": \"idle\","
    "       \"connection_state\": \"online\","
    "       \"manufacturer\": \"Google\","
    "       \"model\": \"Google Chrome\","
    "       \"serial_number\": \"1111-22222-33333-4444\","
    "       \"firmware\": \"24.0.1312.52\","
    "       \"uptime\": 600,"
    "       \"setup_url\": \"http://support.google.com/\","
    "       \"support_url\": \"http://support.google.com/cloudprint/?hl=en\","
    "       \"update_url\": \"http://support.google.com/cloudprint/?hl=en\","
    "       \"x-privet-token\": \"SampleTokenForTesting\","
    "       \"api\": ["
    "               \"/privet/accesstoken\","
    "               \"/privet/capabilities\","
    "               \"/privet/printer/submitdoc\","
    "       ]"
    "}";

const char kSampleInfoResponseWithCreatejob[] = "{"
    "       \"version\": \"1.0\","
    "       \"name\": \"Common printer\","
    "       \"description\": \"Printer connected through Chrome connector\","
    "       \"url\": \"https://www.google.com/cloudprint\","
    "       \"type\": ["
    "               \"printer\""
    "       ],"
    "       \"id\": \"\","
    "       \"device_state\": \"idle\","
    "       \"connection_state\": \"online\","
    "       \"manufacturer\": \"Google\","
    "       \"model\": \"Google Chrome\","
    "       \"serial_number\": \"1111-22222-33333-4444\","
    "       \"firmware\": \"24.0.1312.52\","
    "       \"uptime\": 600,"
    "       \"setup_url\": \"http://support.google.com/\","
    "       \"support_url\": \"http://support.google.com/cloudprint/?hl=en\","
    "       \"update_url\": \"http://support.google.com/cloudprint/?hl=en\","
    "       \"x-privet-token\": \"SampleTokenForTesting\","
    "       \"api\": ["
    "               \"/privet/accesstoken\","
    "               \"/privet/capabilities\","
    "               \"/privet/printer/createjob\","
    "               \"/privet/printer/submitdoc\","
    "       ]"
    "}";

const char kSampleRegisterStartResponse[] = "{"
    "\"user\": \"example@google.com\","
    "\"action\": \"start\""
    "}";

const char kSampleRegisterGetClaimTokenResponse[] = "{"
    "       \"action\": \"getClaimToken\","
    "       \"user\": \"example@google.com\","
    "       \"token\": \"MySampleToken\","
    "       \"claim_url\": \"https://domain.com/SoMeUrL\""
    "}";

const char kSampleRegisterCompleteResponse[] = "{"
    "\"user\": \"example@google.com\","
    "\"action\": \"complete\","
    "\"device_id\": \"MyDeviceID\""
    "}";

const char kSampleXPrivetErrorResponse[] =
    "{ \"error\": \"invalid_x_privet_token\" }";

const char kSampleRegisterErrorTransient[] =
    "{ \"error\": \"device_busy\", \"timeout\": 1}";

const char kSampleRegisterErrorPermanent[] =
    "{ \"error\": \"user_cancel\" }";

const char kSampleInfoResponseBadJson[] = "{";

const char kSampleRegisterCancelResponse[] = "{"
    "\"user\": \"example@google.com\","
    "\"action\": \"cancel\""
    "}";

const char kSampleLocalPrintResponse[] = "{"
    "\"job_id\": \"123\","
    "\"expires_in\": 500,"
    "\"job_type\": \"application/pdf\","
    "\"job_size\": 16,"
    "\"job_name\": \"Sample job name\","
    "}";

const char kSampleCapabilitiesResponse[] = "{"
    "\"version\" : \"1.0\","
    "\"printer\" : {"
    "  \"supported_content_type\" : ["
    "   { \"content_type\" : \"application/pdf\" },"
    "   { \"content_type\" : \"image/pwg-raster\" }"
    "  ]"
    "}"
    "}";

const char kSampleCapabilitiesResponsePWGOnly[] = "{"
    "\"version\" : \"1.0\","
    "\"printer\" : {"
    "  \"supported_content_type\" : ["
    "   { \"content_type\" : \"image/pwg-raster\" }"
    "  ]"
    "}"
    "}";

const char kSampleCreatejobResponse[] = "{ \"job_id\": \"1234\" }";

class MockTestURLFetcherFactoryDelegate
    : public net::TestURLFetcher::DelegateForTests {
 public:
  // Callback issued correspondingly to the call to the |Start()| method.
  MOCK_METHOD1(OnRequestStart, void(int fetcher_id));

  // Callback issued correspondingly to the call to |AppendChunkToUpload|.
  // Uploaded chunks can be retrieved with the |upload_chunks()| getter.
  MOCK_METHOD1(OnChunkUpload, void(int fetcher_id));

  // Callback issued correspondingly to the destructor.
  MOCK_METHOD1(OnRequestEnd, void(int fetcher_id));
};

class PrivetHTTPTest : public ::testing::Test {
 public:
  PrivetHTTPTest() {
    request_context_= new net::TestURLRequestContextGetter(
        base::MessageLoopProxy::current());
    privet_client_.reset(new PrivetHTTPClientImpl(
        "sampleDevice._privet._tcp.local",
        net::HostPortPair("10.0.0.8", 6006),
        request_context_.get()));
    fetcher_factory_.SetDelegateForTests(&fetcher_delegate_);
  }

  virtual ~PrivetHTTPTest() {
  }

  bool SuccessfulResponseToURL(const GURL& url,
                               const std::string& response) {
    return SuccessfulResponseToURLAndData(url, "", response);
  }

  bool SuccessfulResponseToURLAndData(const GURL& url,
                                      const std::string& data,
                                      const std::string& response) {
    net::TestURLFetcher* fetcher = fetcher_factory_.GetFetcherByID(0);
    EXPECT_TRUE(fetcher);
    EXPECT_EQ(url, fetcher->GetOriginalURL());

    if (!data.empty()) {
      EXPECT_EQ(data, fetcher->upload_data());
    }

    if (!fetcher || url != fetcher->GetOriginalURL() ||
        (!data.empty() && data != fetcher->upload_data()))
      return false;

    fetcher->SetResponseString(response);
    fetcher->set_status(net::URLRequestStatus(net::URLRequestStatus::SUCCESS,
                                              net::OK));
    fetcher->set_response_code(200);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    return true;
  }

 protected:
  base::MessageLoop loop_;
  scoped_refptr<net::TestURLRequestContextGetter> request_context_;
  net::TestURLFetcherFactory fetcher_factory_;
  scoped_ptr<PrivetHTTPClient> privet_client_;
  NiceMock<MockTestURLFetcherFactoryDelegate> fetcher_delegate_;
};

class MockInfoDelegate : public PrivetInfoOperation::Delegate {
 public:
  MockInfoDelegate() {}
  ~MockInfoDelegate() {}

  virtual void OnPrivetInfoDone(PrivetInfoOperation* operation,
                                int response_code,
                                const base::DictionaryValue* value) OVERRIDE {
    if (!value) {
      value_.reset();
    } else {
      value_.reset(value->DeepCopy());
    }

    OnPrivetInfoDoneInternal(response_code);
  }

  MOCK_METHOD1(OnPrivetInfoDoneInternal, void(int response_code));

  const base::DictionaryValue* value() { return value_.get(); }
 protected:
  scoped_ptr<base::DictionaryValue> value_;
};

class MockCapabilitiesDelegate : public PrivetCapabilitiesOperation::Delegate {
 public:
  MockCapabilitiesDelegate() {}
  ~MockCapabilitiesDelegate() {}

  virtual void OnPrivetCapabilities(
      PrivetCapabilitiesOperation* operation,
      int response_code,
      const base::DictionaryValue* value) OVERRIDE {
    if (!value) {
      value_.reset();
    } else {
      value_.reset(value->DeepCopy());
    }

    OnPrivetCapabilitiesDoneInternal(response_code);
  }

  MOCK_METHOD1(OnPrivetCapabilitiesDoneInternal, void(int response_code));

  const base::DictionaryValue* value() { return value_.get(); }
 protected:
  scoped_ptr<base::DictionaryValue> value_;
};

class MockRegisterDelegate : public PrivetRegisterOperation::Delegate {
 public:
  MockRegisterDelegate() {
  }
  ~MockRegisterDelegate() {
  }

  virtual void OnPrivetRegisterClaimToken(
      PrivetRegisterOperation* operation,
      const std::string& token,
      const GURL& url) OVERRIDE {
    OnPrivetRegisterClaimTokenInternal(token, url);
  }

  MOCK_METHOD2(OnPrivetRegisterClaimTokenInternal, void(
      const std::string& token,
      const GURL& url));

  virtual void OnPrivetRegisterError(
      PrivetRegisterOperation* operation,
      const std::string& action,
      PrivetRegisterOperation::FailureReason reason,
      int printer_http_code,
      const DictionaryValue* json) OVERRIDE {
    // TODO(noamsml): Save and test for JSON?
    OnPrivetRegisterErrorInternal(action, reason, printer_http_code);
  }

  MOCK_METHOD3(OnPrivetRegisterErrorInternal,
               void(const std::string& action,
                    PrivetRegisterOperation::FailureReason reason,
                    int printer_http_code));

  virtual void OnPrivetRegisterDone(
      PrivetRegisterOperation* operation,
      const std::string& device_id) OVERRIDE {
    OnPrivetRegisterDoneInternal(device_id);
  }

  MOCK_METHOD1(OnPrivetRegisterDoneInternal,
               void(const std::string& device_id));
};

class MockLocalPrintDelegate : public PrivetLocalPrintOperation::Delegate {
 public:
  MockLocalPrintDelegate() {}
  ~MockLocalPrintDelegate() {}

  virtual void OnPrivetPrintingRequestPDF(
      const PrivetLocalPrintOperation* print_operation) {
    OnPrivetPrintingRequestPDFInternal();
  }

  MOCK_METHOD0(OnPrivetPrintingRequestPDFInternal, void());

  virtual void OnPrivetPrintingRequestPWGRaster(
      const PrivetLocalPrintOperation* print_operation) {
    OnPrivetPrintingRequestPWGRasterInternal();
  }

  MOCK_METHOD0(OnPrivetPrintingRequestPWGRasterInternal, void());

  virtual void OnPrivetPrintingDone(
      const PrivetLocalPrintOperation* print_operation) {
    OnPrivetPrintingDoneInternal();
  }

  MOCK_METHOD0(OnPrivetPrintingDoneInternal, void());

  virtual void OnPrivetPrintingError(
      const PrivetLocalPrintOperation* print_operation, int http_code) {
    OnPrivetPrintingErrorInternal(http_code);
  }

  MOCK_METHOD1(OnPrivetPrintingErrorInternal, void(int http_code));
};

class PrivetInfoTest : public PrivetHTTPTest {
 public:
  PrivetInfoTest() {}

  virtual ~PrivetInfoTest() {}

  virtual void SetUp() OVERRIDE {
    info_operation_ = privet_client_->CreateInfoOperation(&info_delegate_);
  }

 protected:
  scoped_ptr<PrivetInfoOperation> info_operation_;
  StrictMock<MockInfoDelegate> info_delegate_;
};

TEST_F(PrivetInfoTest, SuccessfulInfo) {
  info_operation_->Start();

  net::TestURLFetcher* fetcher = fetcher_factory_.GetFetcherByID(0);
  ASSERT_TRUE(fetcher != NULL);
  EXPECT_EQ(GURL("http://10.0.0.8:6006/privet/info"),
            fetcher->GetOriginalURL());

  fetcher->SetResponseString(kSampleInfoResponse);
  fetcher->set_status(net::URLRequestStatus(net::URLRequestStatus::SUCCESS,
                                            net::OK));
  fetcher->set_response_code(200);

  EXPECT_CALL(info_delegate_, OnPrivetInfoDoneInternal(200));
  fetcher->delegate()->OnURLFetchComplete(fetcher);

  std::string name;

  privet_client_->GetCachedInfo()->GetString("name", &name);
  EXPECT_EQ("Common printer", name);
};

TEST_F(PrivetInfoTest, InfoSaveToken) {
  info_operation_->Start();

  net::TestURLFetcher* fetcher = fetcher_factory_.GetFetcherByID(0);
  ASSERT_TRUE(fetcher != NULL);
  fetcher->SetResponseString(kSampleInfoResponse);
  fetcher->set_status(net::URLRequestStatus(net::URLRequestStatus::SUCCESS,
                                            net::OK));
  fetcher->set_response_code(200);

  EXPECT_CALL(info_delegate_, OnPrivetInfoDoneInternal(200));
  fetcher->delegate()->OnURLFetchComplete(fetcher);

  info_operation_ = privet_client_->CreateInfoOperation(&info_delegate_);
  info_operation_->Start();

  fetcher = fetcher_factory_.GetFetcherByID(0);
  ASSERT_TRUE(fetcher != NULL);
  net::HttpRequestHeaders headers;
  fetcher->GetExtraRequestHeaders(&headers);
  std::string header_token;
  ASSERT_TRUE(headers.GetHeader("X-Privet-Token", &header_token));
  EXPECT_EQ("SampleTokenForTesting", header_token);
};

TEST_F(PrivetInfoTest, InfoFailureHTTP) {
  info_operation_->Start();

  net::TestURLFetcher* fetcher = fetcher_factory_.GetFetcherByID(0);
  ASSERT_TRUE(fetcher != NULL);
  fetcher->set_status(net::URLRequestStatus(net::URLRequestStatus::SUCCESS,
                                            net::OK));
  fetcher->set_response_code(404);

  EXPECT_CALL(info_delegate_, OnPrivetInfoDoneInternal(404));
  fetcher->delegate()->OnURLFetchComplete(fetcher);
  EXPECT_EQ(NULL, privet_client_->GetCachedInfo());
};

class PrivetRegisterTest : public PrivetHTTPTest {
 public:
  PrivetRegisterTest() {
  }
  virtual ~PrivetRegisterTest() {
  }

  virtual void SetUp() OVERRIDE {
    info_operation_ = privet_client_->CreateInfoOperation(&info_delegate_);
    register_operation_ =
        privet_client_->CreateRegisterOperation("example@google.com",
                                                &register_delegate_);
  }

 protected:
  bool SuccessfulResponseToURL(const GURL& url,
                               const std::string& response) {
    net::TestURLFetcher* fetcher = fetcher_factory_.GetFetcherByID(0);
    EXPECT_TRUE(fetcher);
    EXPECT_EQ(url, fetcher->GetOriginalURL());
    if (!fetcher || url != fetcher->GetOriginalURL())
      return false;

    fetcher->SetResponseString(response);
    fetcher->set_status(net::URLRequestStatus(net::URLRequestStatus::SUCCESS,
                                              net::OK));
    fetcher->set_response_code(200);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    return true;
  }

  void RunFor(base::TimeDelta time_period) {
    base::CancelableCallback<void()> callback(base::Bind(
        &PrivetRegisterTest::Stop, base::Unretained(this)));
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE, callback.callback(), time_period);

    base::MessageLoop::current()->Run();
    callback.Cancel();
  }

  void Stop() {
    base::MessageLoop::current()->Quit();
  }

  scoped_ptr<PrivetInfoOperation> info_operation_;
  NiceMock<MockInfoDelegate> info_delegate_;
  scoped_ptr<PrivetRegisterOperation> register_operation_;
  StrictMock<MockRegisterDelegate> register_delegate_;
};

TEST_F(PrivetRegisterTest, RegisterSuccessSimple) {
  // Start with info request first to populate XSRF token.
  info_operation_->Start();

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/info"),
      kSampleInfoResponse));

  register_operation_->Start();

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/register?"
           "action=start&user=example%40google.com"),
      kSampleRegisterStartResponse));

  EXPECT_CALL(register_delegate_, OnPrivetRegisterClaimTokenInternal(
      "MySampleToken",
      GURL("https://domain.com/SoMeUrL")));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/register?"
           "action=getClaimToken&user=example%40google.com"),
      kSampleRegisterGetClaimTokenResponse));

  register_operation_->CompleteRegistration();

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/register?"
           "action=complete&user=example%40google.com"),
      kSampleRegisterCompleteResponse));

  EXPECT_CALL(register_delegate_, OnPrivetRegisterDoneInternal(
      "MyDeviceID"));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/info"),
      kSampleInfoResponseRegistered));
}

TEST_F(PrivetRegisterTest, RegisterNoInfoCall) {
  register_operation_->Start();

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/info"),
      kSampleInfoResponse));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/register?"
           "action=start&user=example%40google.com"),
      kSampleRegisterStartResponse));
}

TEST_F(PrivetRegisterTest, RegisterXSRFFailure) {
  register_operation_->Start();

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/info"),
      kSampleInfoResponse));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/register?"
           "action=start&user=example%40google.com"),
      kSampleRegisterStartResponse));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/register?"
           "action=getClaimToken&user=example%40google.com"),
      kSampleXPrivetErrorResponse));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/info"),
      kSampleInfoResponse));

  EXPECT_CALL(register_delegate_, OnPrivetRegisterClaimTokenInternal(
      "MySampleToken", GURL("https://domain.com/SoMeUrL")));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/register?"
           "action=getClaimToken&user=example%40google.com"),
      kSampleRegisterGetClaimTokenResponse));
}

TEST_F(PrivetRegisterTest, TransientFailure) {
  register_operation_->Start();

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/info"),
      kSampleInfoResponse));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/register?"
           "action=start&user=example%40google.com"),
      kSampleRegisterErrorTransient));

  EXPECT_CALL(fetcher_delegate_, OnRequestStart(0));

  RunFor(base::TimeDelta::FromSeconds(2));

  testing::Mock::VerifyAndClearExpectations(&fetcher_delegate_);

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/register?"
           "action=start&user=example%40google.com"),
      kSampleRegisterStartResponse));
}

TEST_F(PrivetRegisterTest, PermanentFailure) {
  register_operation_->Start();

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/info"),
      kSampleInfoResponse));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/register?"
           "action=start&user=example%40google.com"),
      kSampleRegisterStartResponse));

  EXPECT_CALL(register_delegate_,
              OnPrivetRegisterErrorInternal(
                  "getClaimToken",
                  PrivetRegisterOperation::FAILURE_JSON_ERROR,
                  200));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/register?"
           "action=getClaimToken&user=example%40google.com"),
      kSampleRegisterErrorPermanent));
}

TEST_F(PrivetRegisterTest, InfoFailure) {
  register_operation_->Start();

  EXPECT_CALL(register_delegate_,
              OnPrivetRegisterErrorInternal(
                  "start",
                  PrivetRegisterOperation::FAILURE_TOKEN,
                  -1));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/info"),
      kSampleInfoResponseBadJson));
}

TEST_F(PrivetRegisterTest, RegisterCancel) {
  // Start with info request first to populate XSRF token.
  info_operation_->Start();

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/info"),
      kSampleInfoResponse));

  register_operation_->Start();

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/register?"
           "action=start&user=example%40google.com"),
      kSampleRegisterStartResponse));

  register_operation_->Cancel();

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/register?"
           "action=cancel&user=example%40google.com"),
      kSampleRegisterCancelResponse));

  // Must keep mocks alive for 3 seconds so the cancelation object can be
  // deleted.
  RunFor(base::TimeDelta::FromSeconds(3));
}

class PrivetCapabilitiesTest : public PrivetHTTPTest {
 public:
  PrivetCapabilitiesTest() {}

  virtual ~PrivetCapabilitiesTest() {}

  virtual void SetUp() OVERRIDE {
    capabilities_operation_ = privet_client_->CreateCapabilitiesOperation(
        &capabilities_delegate_);
  }

 protected:
  scoped_ptr<PrivetCapabilitiesOperation> capabilities_operation_;
  StrictMock<MockCapabilitiesDelegate> capabilities_delegate_;
};

TEST_F(PrivetCapabilitiesTest, SuccessfulCapabilities) {
  capabilities_operation_->Start();

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/info"),
      kSampleInfoResponse));

  EXPECT_CALL(capabilities_delegate_, OnPrivetCapabilitiesDoneInternal(200));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/capabilities"),
      kSampleCapabilitiesResponse));

  std::string version;
  EXPECT_TRUE(capabilities_delegate_.value()->GetString("version", &version));
  EXPECT_EQ("1.0", version);
};

TEST_F(PrivetCapabilitiesTest, CacheToken) {
  capabilities_operation_->Start();

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/info"),
      kSampleInfoResponse));

  EXPECT_CALL(capabilities_delegate_, OnPrivetCapabilitiesDoneInternal(200));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/capabilities"),
      kSampleCapabilitiesResponse));

  capabilities_operation_ = privet_client_->CreateCapabilitiesOperation(
      &capabilities_delegate_);

  capabilities_operation_->Start();

  EXPECT_CALL(capabilities_delegate_, OnPrivetCapabilitiesDoneInternal(200));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/capabilities"),
      kSampleCapabilitiesResponse));
};

TEST_F(PrivetCapabilitiesTest, BadToken) {
  capabilities_operation_->Start();

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/info"),
      kSampleInfoResponse));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/capabilities"),
      kSampleXPrivetErrorResponse));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/info"),
      kSampleInfoResponse));

  EXPECT_CALL(capabilities_delegate_, OnPrivetCapabilitiesDoneInternal(200));

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/capabilities"),
      kSampleCapabilitiesResponse));
};

class PrivetLocalPrintTest : public PrivetHTTPTest {
 public:
  PrivetLocalPrintTest() {}

  virtual ~PrivetLocalPrintTest() {}

  virtual void SetUp() OVERRIDE {
    local_print_operation_ = privet_client_->CreateLocalPrintOperation(
        &local_print_delegate_);
  }

 protected:
  scoped_ptr<PrivetLocalPrintOperation> local_print_operation_;
  StrictMock<MockLocalPrintDelegate> local_print_delegate_;
};

TEST_F(PrivetLocalPrintTest, SuccessfulLocalPrint) {
  local_print_operation_->SetUsername("sample@gmail.com");
  local_print_operation_->SetJobname("Sample job name");
  local_print_operation_->Start();

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/info"),
      kSampleInfoResponse));

  EXPECT_CALL(local_print_delegate_, OnPrivetPrintingRequestPDFInternal());

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/capabilities"),
      kSampleCapabilitiesResponse));

  local_print_operation_->SendData("Sample print data");

  EXPECT_CALL(local_print_delegate_, OnPrivetPrintingDoneInternal());

  // TODO(noamsml): Is encoding spaces as pluses standard?
  EXPECT_TRUE(SuccessfulResponseToURLAndData(
      GURL("http://10.0.0.8:6006/privet/printer/submitdoc?"
           "user=sample%40gmail.com&jobname=Sample+job+name"),
      "Sample print data",
      kSampleLocalPrintResponse));
};

TEST_F(PrivetLocalPrintTest, SuccessfulPWGLocalPrint) {
  local_print_operation_->SetUsername("sample@gmail.com");
  local_print_operation_->SetJobname("Sample job name");
  local_print_operation_->Start();

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/info"),
      kSampleInfoResponse));

  EXPECT_CALL(local_print_delegate_,
              OnPrivetPrintingRequestPWGRasterInternal());

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/capabilities"),
      kSampleCapabilitiesResponsePWGOnly));

  local_print_operation_->SendData("Sample print data");

  EXPECT_CALL(local_print_delegate_, OnPrivetPrintingDoneInternal());

  // TODO(noamsml): Is encoding spaces as pluses standard?
  EXPECT_TRUE(SuccessfulResponseToURLAndData(
      GURL("http://10.0.0.8:6006/privet/printer/submitdoc?"
           "user=sample%40gmail.com&jobname=Sample+job+name"),
      "Sample print data",
      kSampleLocalPrintResponse));
};

TEST_F(PrivetLocalPrintTest, SuccessfulLocalPrintWithCreatejob) {
  local_print_operation_->SetUsername("sample@gmail.com");
  local_print_operation_->SetJobname("Sample job name");
  local_print_operation_->SetTicket("Sample print ticket");
  local_print_operation_->Start();

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/info"),
      kSampleInfoResponseWithCreatejob));

  EXPECT_CALL(local_print_delegate_, OnPrivetPrintingRequestPDFInternal());

  EXPECT_TRUE(SuccessfulResponseToURL(
      GURL("http://10.0.0.8:6006/privet/capabilities"),
      kSampleCapabilitiesResponse));

  local_print_operation_->SendData("Sample print data");

  EXPECT_TRUE(SuccessfulResponseToURLAndData(
      GURL("http://10.0.0.8:6006/privet/printer/createjob"),
      "Sample print ticket",
      kSampleCreatejobResponse));

  EXPECT_CALL(local_print_delegate_, OnPrivetPrintingDoneInternal());

  // TODO(noamsml): Is encoding spaces as pluses standard?
  EXPECT_TRUE(SuccessfulResponseToURLAndData(
      GURL("http://10.0.0.8:6006/privet/printer/submitdoc?"
           "user=sample%40gmail.com&jobname=Sample+job+name&job_id=1234"),
      "Sample print data",
      kSampleLocalPrintResponse));
};


}  // namespace

}  // namespace local_discovery
