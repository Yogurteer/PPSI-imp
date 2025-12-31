# Payable PSI

written by zzp on 10.13

## Description

* 基于Keyword PIR和OT extension的非对称LPSI协议,可应用于datamarket隐私计费场景

## Construction

### 组件构成

Init

- Sender $S$ input $(X,V) = \{(x_1,v_1),...,(x_m,v_m)\}$
- Receiver $C$ input $Y=\{y_1,...,y_n\}$
- Gen Hash $H()$ , $H_1()$ and $H_2()$ agreed by parties

Component 1：DH-OPRF+PRP

1. The Receiver randomly chooses $r_c \xleftarrow{\$} \mathbb{Z}_q^*$ and sends $\{H(y_1)^{r_c},...,H(y_n)^{r_c}\}$ to the Sender.
2. The Sender uses PRP $\pi$ to shuffle the set as $\{H(y_1^*)^{r_c},...,H(y_n^*)^{r_c}\}$. The Sender randomly chooses $r_s \xleftarrow{\$} \mathbb{Z}_q^*$ and computes $\{H(y_1^*)^{r_cr_s},...,H(y_n^*)^{r_cr_s}\}$. It then sends this set to the Receiver.
3. As DH-OPRF ，The Receiver computes $(H(y_i^*)^{r_cr_s})^{r_c^{-1}}=H(y_i^*)^{r_s}$ for all $i\in [n]$ and gets  $Y'=\{H_1(H(y_1^*)^{r_s}),...,H_1(H(y_n^*)^{r_s})\}$.

Component 2：Batch Keyword PIR

1. Sender use 3-way simple hash ,map $\{H_1(H(x_1)^{r_s}),...,H_1(H(x_m)^{r_s})\}$ to buckets with size $b=1.5n$ to get $X^*$.
2. For each bucket $k$, Sender replace each $H_1(H(x_i)^{r_s})$ as $((H_1(H(x_i)^{r_s}),x_i||v_i\oplus H_2(r_k,H(x_i)^{r_s}))$ pair
3. Receiver use 3-way cuckoo hash (same hash as above simple hash), map $\{H_1(H(y_1^*)^{r_s}),...,H_1(H(y_n^*)^{r_s})\}$  to buckets with size $b=1.5n$ to get $Y^*$,where each bucket store single element
4. Sender again use multi-way (name num of hash functions as $nh$) cuckoo hash for elements in each bucket of $X^*$，so that each element is allocated in an index $id$ of a sub bucket.Meanwhile, Receiver can also reuse $nh$ times hash for each element in $Y^*$ to get $nh$ $ids$. After step 1-4，Receiver can execute nearly $nh\cdot n$ index query to get $n$ valid element in $X^*$ corresponding to keyword in $Y^*$, just like the procedure GenSchedule in PIRANA.As shown in fighure follow

   <img src="https://raw.githubusercontent.com/Yogurteer/PicGo_Figure/main/101301.png" alt="image-20250822142310209" style="zoom: 30%;" />
5. Sender and the Receiver operates Batch Keyword PIR based on PIRANA  , satisfy:Receiver query $H_1(H(y_j^*)^{r_s})$ located in bucket $k$ of  $Y^*$,gets $s_k=x_i||v_i\oplus H_2(r_k,H(x_i)^{r_s})$ located in bucket $k$ of $X^*$ , where $x_i=y_j^*$, and position $k\in[b]$,otherwise gets $\bot$.Repeat query process from bucket 1 to b

<img src="https://raw.githubusercontent.com/Yogurteer/PicGo_Figure/main/090801.png" alt="image-20250822142310209" style="zoom: 50%;" />

Component 3：t times 1-out-of-n OT

- Receiver and the Sender operates t times 1-out-of-n OT protocols.
- Goal

  1. The Sender inputs $R=\{r_1,...,r_b\}$, the Receivers inputs the positions $\{i_1,...,i_k\}$ that is not $\empty$ in the step Keyword PIR.
  2. The Receiver gets the wanted $r_i$ and recovers the value as $x_i||v_i=s_i\oplus H_2(r_i,H(x_i)^{r_s})$, where $H(x_i)^{r_s}$ is the value that the Receiver map to bucket $i$. The Sender gets $k$ as the number of executions of the OT protocol.
  3. Instantiation OT based on 17CT-RSA OT-extension protocol
  4. OT-extension like

     <img src="https://raw.githubusercontent.com/Yogurteer/PicGo_Figure/main/090802.png" alt="image-20250822142955743" style="zoom:50%;" />
  5. 17CT-RSA OT-extension protocol
  6. 具体实现

     <img src="https://raw.githubusercontent.com/Yogurteer/PicGo_Figure/main/090803.png" alt="image-20250822142908138" style="zoom:50%;" />

### 组件接口

- Sys init

  - Sender $S$ input $(X,V) = \{(x_1,v_1),...,(x_m,v_m)\}$
  - Receiver $C$ input $Y=\{y_1,...,y_n\}$
  - element type: e format is vector `<unsigned char>`
  - Gen Hash $H()$ , $H_1()$ and $H_2()$ agreed by parties
- DH-OPRF+PRP

  - In:Y from S,(X,V) from C
  - Out:C get $Y'=\{H_1(H(y_1^*)^{r_s}),...,H_1(H(y_n^*)^{r_s})\}$,$Y^*$,format is [e]
- Batch Keyword PIR-PIRANA

  - In:X from S,Y' from C
  - Out:$\{k,s_k|if\quad{query}_k \quad success\}$ where $query_k$ is related to an element in Y', $s_k=x_i||v_i\oplus H_2(r_k,H(x_i)^{r_s})$,format is [[e1,e2]]
- t times 1-out-of-n OT

  - In:$\{r\}_b$ from S, $\{i\}_k$ from C
  - Out: S get judge=|k|,C get $\{ri\}_k$
- Decrypt

  - In:$\{k,s_k|if\quad{query}_k \quad success\}$ and $\{ri\}_k$
  - Out:$I$||$labels$
- Sys output

  - S get $judge$
  - C get $I$||$labels$
