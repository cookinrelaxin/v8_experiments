#include <iostream>
#include <string>

#include "include/libplatform/libplatform.h"
#include "include/v8.h"

using namespace v8;

class ArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
    public:
        virtual void* Allocate(size_t length) {
            void* data = AllocateUninitialized(length);
            return data == NULL ? data : memset(data, 0, length);
        }
        virtual void* AllocateUninitialized(size_t length) { return malloc(length); }
        virtual void Free(void* data, size_t) { free(data); }
};

int main(int argc, char* argv[]) {
    V8::InitializeICU();
    V8::InitializeExternalStartupData(argv[0]);
    Platform* platform = platform::CreateDefaultPlatform();
    V8::InitializePlatform(platform);
    V8::Initialize();

    ArrayBufferAllocator allocator;
    Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = &allocator;
    Isolate* isolate = Isolate::New(create_params);
    {
        Isolate::Scope isolate_scope(isolate);

        HandleScope handle_scope(isolate);

        Local<Context> context = Context::New(isolate);

        Context::Scope context_scope(context);

        std::string s = R"(
            function print_x(x) {
                return x;
            }
            print_x(17);
        )";

        Local<String> source = 
            String::NewFromUtf8(isolate, s.c_str(),
                                NewStringType::kNormal).ToLocalChecked();
        Local<Script> script = Script::Compile(context, source).ToLocalChecked();

        Local<Value> result = script->Run(context).ToLocalChecked();

        String::Utf8Value utf8(result);
        printf("%s\n", *utf8);
    }
    isolate->Dispose();
    V8::Dispose();
    V8::ShutdownPlatform();
    delete platform;
    return 0;
}

