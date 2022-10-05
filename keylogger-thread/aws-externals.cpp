#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/ivs/IVSClient.h>
#include <aws/ivs/IVSRequest.h>
#include <aws/ivs/model/PutMetadataRequest.h>

#include "aws-externals.h"

using namespace Aws;
using namespace Aws::Auth;
using namespace Aws::IVS;
using namespace Aws::IVS::Model;

extern "C" int sendMetadataRequest(char* channel, char* message)
{
	PutMetadataOutcome putMetadataOutcome;
	SDKOptions options;
	AWSCredentials credentials;

	const char* AWS_AccessKeyId = std::getenv("AWS_ACCESS_KEY_ID");
	const char* AWS_AccessSecretKey = std::getenv("AWS_SECRET_ACCESS_KEY");

	credentials.SetAWSAccessKeyId(AWS_AccessKeyId);
	credentials.SetAWSSecretKey(AWS_AccessSecretKey);

	InitAPI(options);
	{
		IVSClient client{credentials};

		PutMetadataRequest putMetadataRequest;
		putMetadataRequest.WithChannelArn(channel).WithMetadata(message);

		putMetadataOutcome = client.PutMetadata(putMetadataRequest);
	}
	ShutdownAPI(options);

	if (!putMetadataOutcome.IsSuccess())
	{
		return -1;
	}

	return 0;
}

