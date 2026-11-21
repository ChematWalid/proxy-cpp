# Simple HTTP Forward Proxy

## To test

```bash
g++ proxy.cpp -o proxy

./proxy port

curl -x http://ip:port http://www.example.com
```
