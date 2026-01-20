package bgu.spl.net.srv;

import java.io.*;
import java.net.Socket;
import java.nio.charset.StandardCharsets;

public class DatabaseService {

    private final String host;
    private final int port;
    private Socket socket;
    private BufferedWriter out;
    private BufferedReader in;

    public DatabaseService(String host, int port) {
        this.host = host;
        this.port = port;
    }

    public boolean connect() {
        try {
            this.socket = new Socket(host, port);
            this.out = new BufferedWriter(new OutputStreamWriter(socket.getOutputStream(), StandardCharsets.UTF_8));
            this.in = new BufferedReader(new InputStreamReader(socket.getInputStream(), StandardCharsets.UTF_8));
            return true;
        } catch (IOException e) {
            System.err.println("Failed to connect to Python DB Server: " + e.getMessage());
            return false;
        }
    }

    public synchronized String execute(String command) {
        if (socket == null || socket.isClosed()) {
            if (!connect()) return "error: db disconnected";
        }
        try {

            out.write(command);
            out.write('\0'); 
            out.flush();

            StringBuilder sb = new StringBuilder();
            int c;
            while ((c = in.read()) != -1) {
                if (c == '\0') break; 
                sb.append((char) c);
            }
            return sb.toString();

        } catch (IOException e) {
            return "error: " + e.getMessage();
        }
    }
}