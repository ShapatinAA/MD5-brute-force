from flask import Flask, render_template_string, redirect, url_for, request
import docker
import yaml
import os
import requests

app = Flask(__name__)
client = docker.from_env()

COMPOSE_FILE = os.path.join(os.path.dirname(__file__), "..\\docker\\compose.yaml")

TEMPLATE = """
<!doctype html>
<html>
<head>
    <title>Docker Compose Control</title>
</head>
<body>
    <h1>Docker Container Control Panel</h1>
    <ul>
    {% for name, status in statuses.items() %}
        <li>
            <strong>{{ name }}</strong> — {{ status }}
            {% if status != 'running' %}
                <form method="post" action="/start/{{ name }}" style="display:inline;">
                    <button type="submit">Start</button>
                </form>
            {% endif %}
            {% if status == 'running' %}
                <form method="post" action="/stop/{{ name }}" style="display:inline;">
                    <button type="submit">Stop</button>
                </form>
            {% endif %}
        </li>
    {% endfor %}
    </ul>

    <hr>
    <h2>Crack a Hash</h2>
    <form method="post" action="/crack">
        <label for="hash">Hash:</label>
        <input type="text" name="hash" required>
        <br>
        <label for="maxLength">Max Length:</label>
        <input type="number" name="maxLength" required>
        <br>
        <button type="submit">Send Request</button>
    </form>

    {% if crack_result %}
        <h3>Crack Result:</h3>
        <pre>{{ crack_result }}</pre>
    {% endif %}

    <hr>
    <h2>Check Request Status</h2>
    <form method="get" action="/status">
        <label for="request_id">Request ID:</label>
        <input type="text" name="request_id" required>
        <br>
        <button type="submit">Check Status</button>
    </form>

    {% if status_result %}
        <h3>Status Result:</h3>
        <pre>{{ status_result }}</pre>
    {% endif %}
</body>
</html>
"""

def get_service_names():
    try:
        with open(COMPOSE_FILE, 'r') as f:
            compose_data = yaml.safe_load(f)
        return list(compose_data.get('services', {}).keys())
    except Exception as e:
        print(f"[Error] Failed to parse docker-compose.yml: {e}")
        return []

def get_container_statuses():
    statuses = {}
    try:
        services = get_service_names()
        containers = client.containers.list(all=True)
        for service in services:
            container = next(
                (c for c in containers if c.labels.get("com.docker.compose.service") == service),
                None
            )
            if container:
                statuses[service] = container.status
            else:
                statuses[service] = "not created"
    except Exception as e:
        print(f"[Error] Failed to get container statuses: {e}")
    return statuses

@app.route("/")
def index():
    statuses = get_container_statuses()
    return render_template_string(TEMPLATE, statuses=statuses, crack_result=None, status_result=None)

@app.route("/start/<name>", methods=["POST"])
def start_container(name):
    try:
        containers = client.containers.list(all=True)
        container = next((c for c in containers if name in c.name), None)
        if container:
            container.start()
            print(f"[Info] Started container: {name}")
    except Exception as e:
        print(f"[Error] Failed to start container {name}: {e}")
    return redirect(url_for("index"))

@app.route("/stop/<name>", methods=["POST"])
def stop_container(name):
    try:
        containers = client.containers.list()
        container = next((c for c in containers if name in c.name), None)
        if container:
            container.stop()
            print(f"[Info] Stopped container: {name}")
    except Exception as e:
        print(f"[Error] Failed to stop container {name}: {e}")
    return redirect(url_for("index"))

@app.route("/crack", methods=["POST"])
def crack_hash():
    hash_val = request.form.get("hash")
    max_length = request.form.get("maxLength")
    crack_result = None
    try:
        response = requests.post(
            "http://localhost:8848/api/hash/crack",
            json={"hash": hash_val, "maxLength": int(max_length)}
        )
        crack_result = response.text
    except Exception as e:
        crack_result = f"[Error] Failed to send crack request: {e}"

    statuses = get_container_statuses()
    return render_template_string(TEMPLATE, statuses=statuses, crack_result=crack_result, status_result=None)

@app.route("/status")
def check_status():
    request_id = request.args.get("request_id")
    status_result = None
    try:
        response = requests.get(
            f"http://localhost:8848/api/hash/status",
            params={"request_id": request_id}
        )
        status_result = response.text
    except Exception as e:
        status_result = f"[Error] Failed to get status: {e}"

    statuses = get_container_statuses()
    return render_template_string(TEMPLATE, statuses=statuses, crack_result=None, status_result=status_result)

if __name__ == "__main__":
    app.run(port=5000)
