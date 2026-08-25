if(NOT DEFINED INPUT OR NOT DEFINED RUNTIME OR NOT DEFINED OPENSSL OR NOT DEFINED PYTHON OR NOT DEFINED WORK)
    message(FATAL_ERROR "INPUT, RUNTIME, OPENSSL, PYTHON and WORK are required")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

set(certificate "${WORK}/localhost-cert.pem")
set(private_key "${WORK}/localhost-key.pem")
execute_process(
    COMMAND "${OPENSSL}" req -x509 -newkey rsa:2048 -nodes -sha256
        -keyout "${private_key}"
        -out "${certificate}"
        -subj "/CN=localhost"
        -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
        -days 1
    RESULT_VARIABLE certificate_result
    OUTPUT_VARIABLE certificate_output
    ERROR_VARIABLE certificate_error
)
if(NOT certificate_result EQUAL 0)
    message(FATAL_ERROR "Could not create the local TLS certificate:\n${certificate_output}\n${certificate_error}")
endif()
set(wrong_certificate "${WORK}/wrong-ca.pem")
set(wrong_private_key "${WORK}/wrong-ca-key.pem")
execute_process(
    COMMAND "${OPENSSL}" req -x509 -newkey rsa:2048 -nodes -sha256
        -keyout "${wrong_private_key}"
        -out "${wrong_certificate}"
        -subj "/CN=untrusted.local"
        -days 1
    RESULT_VARIABLE wrong_certificate_result
    OUTPUT_VARIABLE wrong_certificate_output
    ERROR_VARIABLE wrong_certificate_error
)
if(NOT wrong_certificate_result EQUAL 0)
    message(FATAL_ERROR "Could not create the negative-test CA:\n${wrong_certificate_output}\n${wrong_certificate_error}")
endif()

set(server_script "${WORK}/server.py")
file(WRITE "${server_script}" [=[
import http.server
import os
import ssl
import sys

certificate, private_key = sys.argv[1], sys.argv[2]

class FixtureHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        if self.path != "/fixture?x=1" or self.headers.get("X-TL-Fixture") != "yes":
            self.send_response(400)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        body = b"wininet-response\n"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        return

server = http.server.HTTPServer(("127.0.0.1", 0), FixtureHandler)
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(certificate, private_key)
server.socket = context.wrap_socket(server.socket, server_side=True)
port = server.server_port
child = os.fork()
if child:
    print(port, flush=True)
    sys.exit(0)

with open(os.devnull, "w") as null:
    os.dup2(null.fileno(), 1)
    os.dup2(null.fileno(), 2)
    server.timeout = 10
    server.handle_request()
    server.server_close()
]=])

function(start_tls_server certificate_file private_key_file port_variable)
    execute_process(
        COMMAND "${PYTHON}" "${server_script}" "${certificate_file}" "${private_key_file}"
        RESULT_VARIABLE server_result
        OUTPUT_VARIABLE server_port
        ERROR_VARIABLE server_error
    )
    if(NOT server_result EQUAL 0)
        string(FIND "${server_error}" "Operation not permitted" socket_denied)
        string(FIND "${server_error}" "Permission denied" socket_denied_alt)
        if(NOT socket_denied EQUAL -1 OR NOT socket_denied_alt EQUAL -1)
            message(FATAL_ERROR "Skipping WinINet TLS fixture: the environment denies loopback sockets")
        endif()
        message(FATAL_ERROR "Could not start the local TLS server:\n${server_error}")
    endif()
    string(STRIP "${server_port}" server_port)
    if(NOT server_port MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "Local TLS server did not provide a valid port: '${server_port}'")
    endif()
    set("${port_variable}" "${server_port}" PARENT_SCOPE)
endfunction()

start_tls_server("${certificate}" "${private_key}" server_port)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "TL_PREFIX=${WORK}/prefix"
        "TL_WININET_CA_FILE=${certificate}"
        "TL_WININET_PORT=${server_port}"
        "${RUNTIME}" --trace "${INPUT}"
    RESULT_VARIABLE runtime_result
    OUTPUT_VARIABLE runtime_stdout
    ERROR_VARIABLE runtime_trace
    TIMEOUT 20
)
if(NOT runtime_result EQUAL 0)
    message(FATAL_ERROR "WinINet fixture returned ${runtime_result}:\n${runtime_trace}")
endif()
if(NOT runtime_stdout STREQUAL "wininet\n")
    message(FATAL_ERROR "Unexpected WinINet fixture stdout: '${runtime_stdout}'\n${runtime_trace}")
endif()
foreach(required_trace
        "resolved dll=\"WININET.dll\" symbol=\"InternetOpenW\""
        "wininet operation=\"open\" status=\"success\""
        "wininet operation=\"send\" status=\"success\""
        "wininet operation=\"read\" status=\"success\"")
    string(FIND "${runtime_trace}" "${required_trace}" trace_position)
    if(trace_position EQUAL -1)
        message(FATAL_ERROR "Trace does not contain '${required_trace}':\n${runtime_trace}")
    endif()
endforeach()

start_tls_server("${certificate}" "${private_key}" wrong_ca_port)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "TL_PREFIX=${WORK}/wrong-ca-prefix"
        "TL_WININET_CA_FILE=${wrong_certificate}"
        "TL_WININET_PORT=${wrong_ca_port}"
        "${RUNTIME}" --trace "${INPUT}"
    RESULT_VARIABLE wrong_ca_result
    OUTPUT_VARIABLE wrong_ca_stdout
    ERROR_VARIABLE wrong_ca_trace
    TIMEOUT 20
)
if(NOT wrong_ca_result EQUAL 4)
    message(FATAL_ERROR "WinINet fixture with an untrusted CA returned ${wrong_ca_result}, expected 4:\n${wrong_ca_trace}")
endif()
string(FIND "${wrong_ca_trace}" "wininet operation=\"send\" status=\"transport-error\"" wrong_ca_trace_position)
if(wrong_ca_trace_position EQUAL -1)
    message(FATAL_ERROR "Untrusted CA was not diagnosed as a WinINet transport failure:\n${wrong_ca_trace}")
endif()
