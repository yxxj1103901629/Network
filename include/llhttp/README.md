
# llhttp - HTTP/1.1 Parser

## 1. 库用途

**llhttp** 是一个高效的 **HTTP/1.1 解析库**，专门用于解析 **HTTP 请求** 和 **响应**。它适用于需要快速解析 HTTP 数据的场景，特别是在嵌入式系统或高性能应用中。该库的设计目标是 **低内存占用** 和 **高性能**，适合于需要处理大量并发请求的环境。

### **主要特点：**
- 高效的 HTTP 请求和响应解析。
- 支持 **HTTP/1.1** 协议。
- 低内存占用，适合嵌入式系统。
- **无依赖**，不需要其他第三方库。

## 2. 使用方法

### 2.1 初始化解析器

首先，您需要初始化解析器和设置回调函数：

```c
#include "llhttp.h"

llhttp_t parser;
llhttp_settings_t settings;

// 初始化设置
llhttp_settings_init(&settings);

// 设置回调函数
settings.on_body = [](llhttp_t *parser, const char *at, size_t length) {
    // 处理接收到的响应体
    printf("Body: %.*s\n", (int) length, at);
    return 0;
};

settings.on_message_complete = [](llhttp_t *parser) -> int {
    // 完成消息解析
    printf("Message Complete\n");
    return 0;
};

// 初始化解析器
llhttp_init(&parser, HTTP_RESPONSE, &settings);
````

### 2.2 解析 HTTP 数据流

接收到 HTTP 数据后，使用 `llhttp_parser_execute` 来解析数据：

```c
const char *response_data = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, world!";
size_t data_length = strlen(response_data);

// 执行解析
llhttp_parser_execute(&parser, response_data, data_length);
```

### 2.3 完整示例

以下是一个简单的使用示例：

```c
#include "llhttp.h"
#include <stdio.h>
#include <string.h>

int main() {
    llhttp_t parser;
    llhttp_settings_t settings;

    // 初始化设置
    llhttp_settings_init(&settings);

    // 设置回调函数
    settings.on_body = [](llhttp_t *parser, const char *at, size_t length) {
        printf("Body: %.*s\n", (int) length, at);
        return 0;
    };

    settings.on_message_complete = [](llhttp_t *parser) -> int {
        printf("Message Complete\n");
        return 0;
    };

    // 初始化解析器
    llhttp_init(&parser, HTTP_RESPONSE, &settings);

    // 模拟收到 HTTP 数据
    const char *response_data = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, world!";
    size_t data_length = strlen(response_data);

    // 解析 HTTP 数据
    llhttp_parser_execute(&parser, response_data, data_length);

    return 0;
}
```

## 3. 总结

**llhttp** 是一个轻量级、高效的 HTTP 解析库，专为高性能需求和低资源环境设计。它不依赖任何外部库，适用于需要快速解析 HTTP 请求和响应的场景，特别是在嵌入式和高并发环境中。
