#include <iostream>

#include <boost/lexical_cast.hpp>
#include <boost/fusion/adapted.hpp>

#include "restc-cpp/restc-cpp.h"
#include "restc-cpp/RequestBuilder.h"
#include "restc-cpp/IteratorFromJsonSerializer.h"

using namespace std;
using namespace restc_cpp;

// C++ structure that match the Json entries received
// from http://jsonplaceholder.typicode.com/posts/{id}
struct Post {
    int userId = 0;
    int id = 0;
    string title;
    string body;
};

// Add C++ reflection to the Post structure.
// This allows our Json (de)serialization to do it's magic.
BOOST_FUSION_ADAPT_STRUCT(
    Post,
    (int, userId)
    (int, id)
    (string, title)
    (string, body)
)

// The C++ main function - the place where any adventure starts
void first() {

    // Create and instantiate a Post from data received from the server.
    Post my_post = RestClient::Create()->ProcessWithPromiseT<Post>([&](Context& ctx) {
        // This is a co-routine, running in a worker-thread

        // Instantiate a Post structure.
        Post post;

        // Serialize it asynchronously. The asynchronously part does not really matter
        // here, but it may if you receive huge data structures.
        SerializeFromJson(post,

            // Construct a request to the server
            RequestBuilder(ctx)
                .Get("http://jsonplaceholder.typicode.com/posts/1")

                // Add some headers for good taste
                .Header("X-Client", "RESTC_CPP")
                .Header("X-Client-Purpose", "Testing")

                // Send the request
                .Execute());

        // Return the post instance trough a C++ future<>
        return post;
    })

    // Get the Post instance from the future<>, or any C++ exception thrown
    // within the lambda.
    .get();

    // Print the result for everyone to see.
    cout << "Received post# " << my_post.id << ", title: " << my_post.title;
}


void DoSomethingInteresting(Context& ctx) {
    // Here we are again in a co-routine, running in a worker-thread.

    // Asynchronously connect to a server and fetch some data.
    auto reply = ctx.Get("http://jsonplaceholder.typicode.com/posts/1");

    // Asynchronously fetch the entire data-set and return it as a string.
    auto json = reply->GetBodyAsString();

    // Just dump the data.
    cout << "Received data: " << json << endl;
}

void second() {
    auto rest_client = RestClient::Create();

    // Call DoSomethingInteresting as a co-routine in a worker-thread.
    rest_client->Process(DoSomethingInteresting);

    // Wait for the request to finish
    rest_client->CloseWhenReady(true);
}

void third() {

    auto rest_client = RestClient::Create();
    rest_client->ProcessWithPromise([&](Context& ctx) {
        // Here we are again in a co-routine, running in a worker-thread.

        // Asynchronously connect to a server and fetch some data.
        auto reply = RequestBuilder(ctx)
            .Get("http://localhost:3001/restricted/posts/1")

            // Authenticate as 'alice' with a very popular password
            .BasicAuthentication("alice", "12345")

            // Send the request.
            .Execute();

        // Dump the well protected data
        cout << "Got: " << reply->GetBodyAsString();

    }).get();
}

void forth() {

    // Add the proxy information to the properties used by the client
    Request::Properties properties;
    properties.proxy.type = Request::Proxy::Type::HTTP;
    properties.proxy.address = "http://127.0.0.1:3003";

    // Create the client with our configuration
    auto rest_client = RestClient::Create(properties);
    rest_client->ProcessWithPromise([&](Context& ctx) {
        // Here we are again in a co-routine, running in a worker-thread.

        // Asynchronously connect to a server trough a HTTP proxy and fetch some data.
        auto reply = RequestBuilder(ctx)
            .Get("http://api.example.com/normal/posts/1")

            // Send the request.
            .Execute();

        // Dump the data
        cout << "Got: " << reply->GetBodyAsString();

    }).get();
}

void fifth() {
    // Fetch a list of records asyncrouesly, one by one.
    // This allows us to process single items in a list
    // and fetching more data as we move forward.
    // This works basically as a database cursor, or
    // (literally) as a properly implemented C++ input iterator.

    // Create the REST clent
    auto rest_client = RestClient::Create();

    // Run our example in a lambda co-routine
    rest_client->Process([&](Context& ctx) {
        // This is the co-routine, running in a worker-thread


        // Construct a request to the server
        auto reply = RequestBuilder(ctx)
            .Get("http://jsonplaceholder.typicode.com/posts/")

            // Add some headers for good taste
            .Header("X-Client", "RESTC_CPP")
            .Header("X-Client-Purpose", "Testing")

            // Send the request
            .Execute();

        // Instatiate a serializer with begin() and end() methods that
        // allows us to work with the reply-data trough a C++
        // input iterator.
        IteratorFromJsonSerializer<Post> data{*reply};

        // Iterate over the data, fetch data asyncrounesly as we go.
        for(const auto& post : data) {
            cout << "Item #" << post.id << " Title: " << post.title << endl;
        }
    });


    // Wait for the request to finish
    rest_client->CloseWhenReady(true);
}

void Sixth() {
    Request::Properties properties;

    // Create the client without creating a worker thread
    auto rest_client = RestClient::Create(properties, true);

    // Add a request to the queue of the io-service in the rest client instance
    rest_client->Process([&](Context& ctx) {
        // Here we are again in a co-routine, now in our own thread.

        // Asynchronously connect to a server trough a HTTP proxy and fetch some data.
        auto reply = RequestBuilder(ctx)
            .Get("http://jsonplaceholder.typicode.com/posts/1")

            // Send the request.
            .Execute();

        // Dump the data
        cout << "Got: " << reply->GetBodyAsString();

        // Shut down the io-service. This will cause run() (below) to return.
        rest_client->CloseWhenReady();

    });

    // Start the io-service, using this thread.
    rest_client->GetIoService().run();

    cout << "Done. Exiting normally." << endl;
}

// Use our own RequestBody implementation to supply
// data to a POST request
void Seventh() {
    // Our own implementation of the raw data provider
    class MyBody : public RequestBody
    {
    public:
        MyBody() = default;

        Type GetType() const noexcept override {

            // This mode causes the request to use chunked data,
            // allowing us to send data without knowing the exact
            // size of the payload when we start.
            return Type::CHUNKED_LAZY_PULL;
        }

        std::uint64_t GetFixedSize() const override {
            throw runtime_error("Not implemented");
        }

        // This will be called until we return false to indicate
        // that we have no further data
        bool GetData(write_buffers_t& buffers) override {

            if (++count_ > 10) {

                // We are done.
                return false;
            }

            ostringstream data;
            data << "This is line #" << count_ << " of the payload.\r\n";

            // The buffer need to persist until we are called again, or the
            // instance is destroyed.
            data_buffer_ = data.str();

            buffers.emplace_back(data_buffer_.c_str(), data_buffer_.size());

            // We added data to buffers, so return true
            return true;
        }

        // Called if we get a HTTP redirect and need to start over again.
        void Reset() override {
            count_ = 0;
        }

    private:
        int count_ = 0;
        string data_buffer_;
    };


    // Create the REST clent
    auto rest_client = RestClient::Create();

    // Run our example in a lambda co-routine
    rest_client->Process([&](Context& ctx) {
        // This is the co-routine, running in a worker-thread

        // Construct a POST request to the server
        RequestBuilder(ctx)
            .Post("http://localhost:3001/upload_raw/")
            .Header("Content-Type", "text/text")
            .Body(make_unique<MyBody>())
            .Execute();
    });


    // Wait for the request to finish
    rest_client->CloseWhenReady(true);
}


int main() {
    try {
//         cout << "First: " << endl;
//         first();
//
//         cout << "Second: " << endl;
//         second();
//
//         cout << "Third: " << endl;
//         third();
//
//         cout << "Forth: " << endl;
//         forth();
//
//         cout << "Fifth: " << endl;
//         fifth();
//
//         cout << "Sixth: " << endl;
//         Sixth();
//
        cout << "Seventh: " << endl;
        Seventh();

    } catch(const exception& ex) {
        cerr << "Something threw up: " << ex.what() << endl;
        return 1;
    }
}
