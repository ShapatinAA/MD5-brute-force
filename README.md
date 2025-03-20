# MD5-brute-force
This is hash cracker for MD5 type of hash. It uses bruteforce.

## Building

To start service just type.
```
cd ./docker && docker compose up
```

## First steps

After that you'll be able to access next endpoints with http requests.
```
POST http://127.0.0.1:8848/api/hash/crack
```
```
Request body:
{
    "hash":"e2fc714c4727ee9395f324cd2e7f331f", 
    "maxLength": 4
}
```
```
Response body:
{
    "requestId":"fc0atk78bcn6ktgl64cv1oank9ffvub13jr45s96k0683d9m3jl2dtraevsjb7le"
}
```
After sending upper request and getting "requestId" value, you'll be able to know your request status via sending next http request, where {requestId} value should be putted in from previous request:
```
GET http://127.0.0.1:8848/api/hash/status?requestId={requestId}
```
```
Response body:
{
    "data": []
    "progress": 0%
    "status": "IN_PROGRESS",
}
```
You'll see final data, when service will have "progress" value as 100%.
```
Response body:
{
    "data": [aa]
    "progress": 78%
    "status": "IN_PROGRESS",
}
```
