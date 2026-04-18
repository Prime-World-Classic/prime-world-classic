from waitress import serve
from client_sync import app, start

if __name__ == '__main__':
    start()
    serve(app, host='127.0.0.1', port='36530', threads=24)
