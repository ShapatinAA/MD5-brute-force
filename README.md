# MD5-brute-force
This is hash cracker for MD5 type of hash. It uses bruteforce.

The difference of this program is that it uses RabbitMQ to communicate with workers and guarantees consistency of information (if sending request was a successfull operation) and partition tolerance 
(if user agrees, that processing of his request will be happening only for some time (300 seconds by default)).

## Building

To start service just type:
```
cd ./docker && docker compose up
```

## First steps

Right after that you'll be able to use 2 different approaches.

### First approach 

To do it run:
```
cd interface && ./.venv/interface/Scripts/activate.ps1 && python app.py
```

And then you can go to http://127.0.0.1:5000. Service web-interface will be avaliable on this endpoint.

You can test out behaviour of the service by turning off and on it's components;

Request hash-cracking by providing hash and maxLength of the hash;

Checking the result of hash cracking done by service.

### Second approach 

You'll be able to access next endpoints with http requests.
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
    "fc0atk78bcn6ktgl64cv1oank9ffvub13jr45s96k0683d9m3jl2dtraevsjb7le"
}
```
After sending upper request and getting value, you'll be able to know your request status via sending next http request, where {requestId} value should be putted in from previous request:
```
GET http://127.0.0.1:8848/api/hash/status?requestId={requestId}
```
```
Response body:
{
    "data": []
    "status": "IN_PROGRESS",
}
```
You'll see final data, when service will have status value as "READY". If network is unreliable or system perfomance is poor you may see "PARTIAL_RESULT" or even "ERROR" statuses, meaning that system got only part of all answers, or timeout fired.
```
Response body:
{
    "data": [aa]
    "status": "READY",
}
```

