#include <include/v8.h>
#include <include/libplatform/libplatform.h>

#include "linenoise.h"

#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <iostream>

v8::Local<v8::Context> CreateShellContext(v8::Isolate* isolate);
void RunShell(v8::Local<v8::Context> context,
              v8::Platform* platform
);
int RunMain(v8::Isolate* isolate,
            v8::Platform* platform,
            int argc,
            char* argv[]
);
bool ExecuteString(v8::Isolate* isolate,
                   v8::Local<v8::String> source,
                   v8::Local<v8::Value> name,
                   bool print_result,
                   bool report_exceptions
);
void Print(const v8::FunctionCallbackInfo<v8::Value>& args);
void Read(const v8::FunctionCallbackInfo<v8::Value>& args);
void Load(const v8::FunctionCallbackInfo<v8::Value>& args);
void Quit(const v8::FunctionCallbackInfo<v8::Value>& args);
void Version(const v8::FunctionCallbackInfo<v8::Value>& args);

v8::MaybeLocal<v8::String> ReadFile(v8::Isolate* isolate,
                                    const char* name
);
void ReportException(v8::Isolate* isolate, v8::TryCatch* handler);

static bool run_shell;

class  ShellArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
    public:
        virtual void* Allocate(size_t length) {
            void* data = AllocateUninitialized(length);
            return data == NULL ? data : memset(data, 0, length);
        }
        virtual void* AllocateUninitialized(size_t length) { return malloc(length); }
        virtual void Free(void* data, size_t) { free(data); }
};

int init(int argc, char *argv[]) {
    v8::V8::InitializeICU();
    v8::V8::InitializeExternalStartupData(argv[0]);
    v8::Platform* platform = v8::platform::CreateDefaultPlatform();
    v8::V8::InitializePlatform(platform);
    v8::V8::Initialize();
    v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
    ShellArrayBufferAllocator array_buffer_allocator;
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = &array_buffer_allocator;
    v8::Isolate* isolate = v8::Isolate::New(create_params);
    run_shell = (argc == 1);
    int result;
    {
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = CreateShellContext(isolate);
        if (context.IsEmpty()) {
            fprintf(stderr, "Error creating context\n");
            return 1;
        }
        v8::Context::Scope context_scope(context);
        result = RunMain(isolate, platform, argc, argv);
        if (run_shell) RunShell(context, platform);
    }
    isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::ShutdownPlatform();
    delete platform;
    return result;
}

// int main(int argc, char* argv[]) {
//     return init(argc, argv);
// }

const char* ToCString(const v8::String::Utf8Value& value) {
    return *value ? *value : "<string conversion failed>";
}

v8::Local<v8::Context> CreateShellContext(v8::Isolate* isolate) {
    v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);

    global->Set(
            v8::String::NewFromUtf8(isolate, "print", v8::NewStringType::kNormal)
                .ToLocalChecked(),
            v8::FunctionTemplate::New(isolate, Print));    

    global->Set(
            v8::String::NewFromUtf8(isolate, "read", v8::NewStringType::kNormal)
                .ToLocalChecked(),
            v8::FunctionTemplate::New(isolate, Read));    

    global->Set(
            v8::String::NewFromUtf8(isolate, "load", v8::NewStringType::kNormal)
                .ToLocalChecked(),
            v8::FunctionTemplate::New(isolate, Load));    

    global->Set(
            v8::String::NewFromUtf8(isolate, "quit", v8::NewStringType::kNormal)
                .ToLocalChecked(),
            v8::FunctionTemplate::New(isolate, Quit));    

    global->Set(
            v8::String::NewFromUtf8(isolate, "version", v8::NewStringType::kNormal)
                .ToLocalChecked(),
            v8::FunctionTemplate::New(isolate, Version));    

    return v8::Context::New(isolate, NULL, global);
}

void Print(const v8::FunctionCallbackInfo<v8::Value>& args) {
    bool first = true;
    for (int i(0); i < args.Length(); i++) {
        v8::HandleScope handle_scope(args.GetIsolate());
        if (first)
            first = false;
        else
            printf(" ");
        v8::String::Utf8Value str(args[i]);
        const char* cstr = ToCString(str);
        printf("%s", cstr);
    }
    printf("\n");
    fflush(stdout);
}

void Read(const v8::FunctionCallbackInfo<v8::Value>& args) {
    if (args.Length() != 1) {
        args.GetIsolate()->ThrowException(
                v8::String::NewFromUtf8(args.GetIsolate(),
                                        "Bad parameters",
                                        v8::NewStringType::kNormal).ToLocalChecked());
        return;
    }
    v8::String::Utf8Value file(args[0]);
    if (*file == NULL) {
        args.GetIsolate()->ThrowException(
                v8::String::NewFromUtf8(args.GetIsolate(),
                                        "Error loading file",
                                        v8::NewStringType::kNormal).ToLocalChecked());
        return;
    }
    v8::Local<v8::String> source;
    if (!ReadFile(args.GetIsolate(), *file).ToLocal(&source)) {
        args.GetIsolate()->ThrowException(
                v8::String::NewFromUtf8(args.GetIsolate(),
                                        "Error loading file",
                                        v8::NewStringType::kNormal).ToLocalChecked());
        return;
    }
    args.GetReturnValue().Set(source);
}

void Load(const v8::FunctionCallbackInfo<v8::Value>& args) {
    for (int i(0); i < args.Length(); i++) {
        v8::HandleScope handle_scope(args.GetIsolate());
        v8::String::Utf8Value file(args[i]);
        if (*file == NULL) {
            args.GetIsolate()->ThrowException(
                    v8::String::NewFromUtf8(args.GetIsolate(),
                                            "Error loading file",
                                            v8::NewStringType::kNormal).ToLocalChecked());
            return;
        }
        v8::Local<v8::String> source;
        if (!ReadFile(args.GetIsolate(), *file).ToLocal(&source)) {
            args.GetIsolate()->ThrowException(
                    v8::String::NewFromUtf8(args.GetIsolate(),
                                            "Error loading file",
                                            v8::NewStringType::kNormal).ToLocalChecked());
            return;
        }
        if (!ExecuteString(args.GetIsolate(), source, args[i], false, false)) {
            args.GetIsolate()->ThrowException(
                    v8::String::NewFromUtf8(args.GetIsolate(),
                                            "Error executing file",
                                            v8::NewStringType::kNormal).ToLocalChecked());
            return;
        }
    }
}

void Quit(const v8::FunctionCallbackInfo<v8::Value>& args) {
    int exit_code =
        args[0]->Int32Value(args.GetIsolate()->GetCurrentContext()).FromMaybe(0);
    fflush(stdout);
    fflush(stderr);
    exit(exit_code);
}

void Version(const v8::FunctionCallbackInfo<v8::Value>& args) {
    args.GetReturnValue().Set(
            v8::String::NewFromUtf8(args.GetIsolate(),
                                    v8::V8::GetVersion(),
                                    v8::NewStringType::kNormal).ToLocalChecked());
}

v8::MaybeLocal<v8::String> ReadFile(v8::Isolate* isolate, const char* name) {
    FILE* file = fopen(name, "rb");
    if (file == NULL) return v8::MaybeLocal<v8::String>();

    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    char* chars = new char[size + 1];
    chars[size] = '\0';
    for (size_t i(0); i < size;) {
        i += fread(&chars[i], 1, size - i, file);
        if (ferror(file)) {
            fclose(file);
            return v8::MaybeLocal<v8::String>();
        }
    }
    fclose(file);
    v8::MaybeLocal<v8::String> result = v8::String::NewFromUtf8(
            isolate,
            chars,
            v8::NewStringType::kNormal,
            static_cast<int>(size));
    delete[] chars;
    return result;
}

int RunMain(v8::Isolate* isolate, v8::Platform* platform, int argc, char* argv[]) {
    for (int i(1); i < argc; i++) {
        const char* str = argv[i];
        if (strcmp(str, "--shell") == 0)
            run_shell = true;
        else if(strcmp(str, "-f") == 0)
            continue;
        else if(strncmp(str, "--", 2) == 0)
            fprintf(stderr,
                    "Warning: unknown flag %s.\nTry --help for options, yo\n",
                    str);
        else if(strcmp(str, "-e") == 0 && i + 1 < argc) {
            v8::Local<v8::String> file_name =
                v8::String::NewFromUtf8(isolate, "unnamed", v8::NewStringType::kNormal)
                .ToLocalChecked();
            v8::Local<v8::String> source;
            if (v8::String::NewFromUtf8(isolate, argv[++i], v8::NewStringType::kNormal)
                    .ToLocal(&source)) {
                return 1;
            }
            bool success = ExecuteString(isolate, source, file_name, false, true);
            while (v8::platform::PumpMessageLoop(platform, isolate)) continue;
            if (!success) return 1;
        }
        else {
            v8::Local<v8::String> file_name =
                v8::String::NewFromUtf8(isolate, str, v8::NewStringType::kNormal)
                .ToLocalChecked();
            v8::Local<v8::String> source;
            if (!ReadFile(isolate, str).ToLocal(&source)) {
                fprintf(stderr, "Error reading '%s'\n", str);
                continue;
            }
            bool success = ExecuteString(isolate, source, file_name, false, true);
            while (v8::platform::PumpMessageLoop(platform, isolate)) continue;
            if (!success) return 1;
        }
    }
    return 0;
}

void completion(const char *buf, linenoiseCompletions *lc) {
    switch(buf[0]) {
        case 'q': {
            linenoiseAddCompletion(lc, "quit()");
            break;
        }
        case 'l': {
            linenoiseAddCompletion(lc, "load()");
            break;
        }
        case 'r': {
            linenoiseAddCompletion(lc, "read()");
            break;
        }
        case 'p': {
            linenoiseAddCompletion(lc, "print()");
            break;
        }
        case 'v': {
            linenoiseAddCompletion(lc, "version()");
            break;
        }
        default:
            break;
    }
}

void RunShell(v8::Local<v8::Context> context,
              v8::Platform* platform) {
    fprintf(stderr, "V8 version %s for ZEngine version 0.1\n", v8::V8::GetVersion());
    fprintf(stderr, "Type quit() or press CTRL-C to quit\n");
    static const int kBufferSize = 256;
    v8::Context::Scope context_scope(context);
    v8::Local<v8::String> name(
            v8::String::NewFromUtf8(context->GetIsolate(),
                                    "(shell)",
                                    v8::NewStringType::kNormal).ToLocalChecked());
    char *line;
    linenoiseSetCompletionCallback(completion);
    while ((line = linenoise("ZEngine> ")) != NULL) {
        linenoiseHistoryAdd(line);
        v8::HandleScope handle_scope(context->GetIsolate());
        ExecuteString(
                context->GetIsolate(),
                v8::String::NewFromUtf8(context->GetIsolate(),
                                        line,
                                        v8::NewStringType::kNormal).ToLocalChecked(),
                name,
                true,
                true);
        while (v8::platform::PumpMessageLoop(platform, context->GetIsolate()))
            continue;
        free(line);
    }
}

bool ExecuteString(v8::Isolate* isolate,
                   v8::Local<v8::String> source,
                   v8::Local<v8::Value> name,
                   bool print_result,
                   bool report_exceptions) {
    v8::HandleScope handle_scope(isolate);
    v8::TryCatch try_catch(isolate);
    v8::ScriptOrigin origin(name);
    v8::Local<v8::Context> context(isolate->GetCurrentContext());
    v8::Local<v8::Script> script;
    if (!v8::Script::Compile(context, source, &origin).ToLocal(&script)) {
        if (report_exceptions)
            ReportException(isolate, &try_catch);
        return false;
    }
    else {
        v8::Local<v8::Value> result;
        if (!script->Run(context).ToLocal(&result)) {
            assert(try_catch.HasCaught());
            if (report_exceptions)
                ReportException(isolate, &try_catch);
            return false;
        }
        else {
            assert(!try_catch.HasCaught());
            if (print_result && !result->IsUndefined()) {
                v8::String::Utf8Value str(result);
                const char* cstr = ToCString(str);
                printf("%s\n", cstr);
            }
            return true;
        }
    }
}

void ReportException(v8::Isolate* isolate, v8::TryCatch* try_catch) {
    v8::HandleScope handle_scope(isolate);
    v8::String::Utf8Value exception(try_catch->Exception());
    const char* exception_string = ToCString(exception);
    v8::Local<v8::Message> message = try_catch->Message();
    if (message.IsEmpty()) {
        fprintf(stderr, "%s\n", exception_string);
    }
    else {
        v8::String::Utf8Value filename(message->GetScriptOrigin().ResourceName());
        v8::Local<v8::Context> context(isolate->GetCurrentContext());
        const char* filename_string = ToCString(filename);
        int linenum = message->GetLineNumber(context).FromJust();
        fprintf(stderr, "%s:%i: %s\n", filename_string, linenum, exception_string);
        v8::String::Utf8Value sourceline(
                message->GetSourceLine(context).ToLocalChecked());
        const char* sourceline_string = ToCString(sourceline);
        fprintf(stderr, "%s\n", sourceline_string);
        int start = message->GetStartColumn(context).FromJust();
        for (int i(0); i < start; i ++) {
            fprintf(stderr, " ");
        }
        int end = message->GetEndColumn(context).FromJust();
        for (int i(start); i < end; i ++) {
            fprintf(stderr, "^");
        }
        fprintf(stderr, "\n");
        v8::Local<v8::Value> stack_trace_string;
        if (try_catch->StackTrace(context).ToLocal(&stack_trace_string) &&
                stack_trace_string->IsString() &&
                v8::Local<v8::String>::Cast(stack_trace_string)->Length() > 0) {
            v8::String::Utf8Value stack_trace(stack_trace_string);
            const char* stack_trace_string = ToCString(stack_trace);
            fprintf(stderr, "%s\n", stack_trace_string);
        }

    }

}
