<h1>CatCoroutine</h1>

**CatCoroutine**是一个基于C++17编写的**有栈协程库**<br>
性能测试:<br>
&emsp;测试平台和编译器：Ubuntu/Gcc13.1<br>
&emsp;测试代码（启动50W个协程序，共切换10亿次）：<br>
![img.png](picture/img.png)<br>
&emsp;测试结果：<br>
&emsp;&emsp;**CatCoroutine**<br>
&emsp;&emsp;![CatCoroutine.png](picture/CatCoroutine.jpg)<br>
&emsp;&emsp;**GoLang**<br>
&emsp;&emsp;![golang.png](picture/golang.jpg)<br>