## Component 3：t times 1-out-of-n OT

* t times 1-out-of-n OT功能

  * In:$\{r\}_b$ from S, $\{i\}_k$ from C
  * Out: S get judge=|k|,C get $\{ri\}_k$
* 调用方案

  * OOS16/CTRSA'17-Actively Secure 1-out-of-N OT Extension with Application to Private Set Intersection.
  * code：https://github.com/osu-crypto/libOTe
  * 复现步骤:
    * 可选直接从当前目录找到libOte,也可选手动git clone https://github.com/osu-crypto/libOTe.git
    * cd libOte
    * python build.py --all --boost --sodium(如果已构建,可跳过)
    * ./out/build/linux/frontend/frontend_libOTe -oos
      * 得到输出类似oos n=1048576 1870 ms
  * 待办:
    * 找到libOte中oos方案的具体实现代码,研究它如何实现t times 1-out-of-n OT以及数据输入输出设置方式,在目录src/设计新的可组合协议模块(最外层方法与本功能输入输出对应)调用oos,支持上述的输入输出,并测试模块正确性和性能(t取[2^9,2^10,2^11],n取[2^16,2^20,2^24],测试样例取正交积)
