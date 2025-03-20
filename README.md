This is hash cracker for MD5 type of hash. It uses bruteforce.
To start service use write docker compose up.
After that you'll be able to access next endpoints with http requests.

POST http://127.0.0.1:8848/api/hash/crack
Request body:
{
    "hash":"e2fc714c4727ee9395f324cd2e7f331f", 
    "maxLength": 4
}
Response body:
{
    "requestId":"fc0atk78bcn6ktgl64cv1oank9ffvub13jr45s96k0683d9m3jl2dtraevsjb7le"
}
После отправки запроса выше и получения значения, можно узнать статус готовности задачи

GET http://127.0.0.1:8848/api/hash/status?requestId={requestId}
Response body:
{
    "status":"IN_PROGRESS",
    "data": null
}
Ответ,  если ответ готов.
Response body:
{
   "status":"READY",
   "data": ["abcd"]
}

