# DLL-Injector
 研究 Dll 远程注入
## 特点：
 使用共享内存传输数据
 自动更新检测
## 编译
  - `InjectDLL.cpp`使用[mingw](https://www.mingw-w64.org/)编译
  - `Injector.cpp`使用`MSVC`编译

  用什么编译器应该没关系……
 ## 使用
  1. cd 到 exe 目录
  2. 在命令行中打开
  3. 运行`.\Injector.exe <PID> <DLL路径>
 ## 注意事项
  1. 注入dll来实施非法目的是违法行为
  2. 杀毒软件可能会拦截 DLL 注入，需要关闭杀软
