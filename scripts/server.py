from flask import Flask, request
import logging

app = Flask(__name__)

@app.route('/price_result', methods=['POST'])
def route1():
    data = request.json  # Assuming the data is sent as JSON
    logging.info("Received data:", data)
    return "OK", 200

@app.route('/route', methods=['POST'])
def route2():
    data = request.json
    logging.info("Received data:", data)
    return "OK", 200

@app.route('/this_route', methods=['POST'])
def route3():
    data = request.json
    logging.info("Received data:", data)
    return "OK", 200

if __name__ == '__main__':
    app.run(debug=False, port=14576)
