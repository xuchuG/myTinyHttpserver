# myTinyHttpserver


# 1

get_line函数将'\r\n','\n','\r'不同系统的换行符都统一成'\n'

# 2

对于在execute_cgi使用两个管道的理解，cgi_output,cig_input；由于根据客户端的请求报文，服务器是需要运行一段新的可执行文件才能得到结果，所以要fork一个子进程，自然可以用到管道通信，又由于管道是单向的，所以要用到两个管道才能完成互相通信。如下图，父进程进程通过cgi_input管道给子进程发送get或者是post报文的数据，由于子进程的in[0]关联到了stdin,所以子进程的可执行程序就会从标准输入里面把数据读出来，处理完之后，子进程的可执行程序将输出打印到标准输出上，又由于stdout关联到out[1],所以这部分数据会写到cgi_output管道中，父进程从out[0]中将数据读出，返回给浏览器客户端

![httpd-管道](C:\Users\Administrator\Desktop\Github项目学习\tinyhttpd\httpd-管道.png)

# 3

cgi文件可由shell脚本，python，c语言（编译成.exe）等语言实现，用于处理一些动态请求（可以不仅仅只是返回一个html的静态界面），本例中使用shell脚本（其中一种perl）实现

# 4

  每个函数的作用：

​     accept_request:  处理从套接字上监听到的一个 HTTP 请求，在这里可以很大一部分地体现服务器处理请求流程。

​     bad_request: 返回给客户端这是个错误请求，HTTP 状态吗 400 BAD REQUEST.

​     cat: 读取服务器上某个文件写到 socket 套接字。

​     cannot_execute: 主要处理发生在执行 cgi 程序时出现的错误。

​     error_die: 把错误信息写到 perror 并退出。

​     execute_cgi: 运行 cgi 程序的处理，也是个主要函数。

​     get_line: 读取套接字的一行，把回车换行等情况都统一为换行符结束。

​     headers: 把 HTTP 响应的头部写到套接字。

​     not_found: 主要处理找不到请求的文件时的情况。

​     sever_file: 调用 cat 把服务器文件返回给浏览器。

​     startup: 初始化 httpd 服务，包括建立套接字，绑定端口，进行监听等。

​     unimplemented: 返回给浏览器表明收到的 HTTP 请求所用的 method 不被支持。



​     建议源码阅读顺序： main -> startup -> accept_request -> execute_cgi, 通晓主要工作流程后再仔细把每个函数的源码看一看。



#### 工作流程

​     （1） 服务器启动，在指定端口或随机选取端口绑定 httpd 服务。

​     （2）收到一个 HTTP 请求时（其实就是 listen 的端口 accpet 的时候），派生一个线程运行 accept_request 函数。

​     （3）取出 HTTP 请求中的 method (GET 或 POST) 和 url,。对于 GET 方法，如果有携带参数，则 query_string 指针指向 url 中 ？ 后面的 GET 参数。

​     （4） 格式化 url 到 path 数组，表示浏览器请求的服务器文件路径，在 tinyhttpd 中服务器文件是在  htdocs 文件夹下。当 url 以 / 结尾，或 url 是个目录，则默认在 path 中加上 index.html，表示访问主页。

​     （5）如果文件路径合法，对于无参数的 GET 请求，直接输出服务器文件到浏览器，即用 HTTP  格式写到套接字上，跳到（10）。其他情况（带参数 GET，POST 方式，url 为可执行文件），则调用 excute_cgi 函数执行 cgi  脚本。

​    （6）读取整个 HTTP 请求并丢弃，如果是 POST 则找出 Content-Length. 把 HTTP 200  状态码写到套接字。

​    （7） 建立两个管道，cgi_input 和 cgi_output, 并 fork 一个进程。

​    （8） 在子进程中，把 STDOUT 重定向到 cgi_outputt 的写入端，把 STDIN 重定向到 cgi_input  的读取端，关闭 cgi_input 的写入端 和 cgi_output 的读取端，设置 request_method 的环境变量，GET  的话设置 query_string 的环境变量，POST 的话设置 content_length 的环境变量，这些环境变量都是为了给 cgi  脚本调用，接着用 execl 运行 cgi 程序。

​    （9） 在父进程中，关闭 cgi_input 的读取端 和 cgi_output 的写入端，如果 POST 的话，把 POST  数据写入 cgi_input，已被重定向到 STDIN，读取 cgi_output 的管道输出到客户端，该管道输入是  STDOUT。接着关闭所有管道，等待子进程结束。这一部分比较乱，见下图说明：

图 1    管道初始状态

![管道初始状态](C:\Users\Administrator\Desktop\Github项目学习\tinyhttpd\管道初始状态.jpg)

 图 2  管道最终状态 

![管道最终状态](C:\Users\Administrator\Desktop\Github项目学习\tinyhttpd\管道最终状态.jpg)

​    （10） 关闭与浏览器的连接，完成了一次 HTTP 请求与回应，因为 HTTP 是无连接的。
