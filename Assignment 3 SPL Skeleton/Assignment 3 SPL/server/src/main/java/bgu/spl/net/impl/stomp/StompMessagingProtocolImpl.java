package bgu.spl.net.impl.stomp;

import bgu.spl.net.api.StompMessagingProtocol;
import bgu.spl.net.srv.Connections;
import bgu.spl.net.srv.DatabaseService;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;

public class StompMessagingProtocolImpl implements StompMessagingProtocol<String>{

    private int connectionId;
    private Connections<String> connections;
    private boolean shouldTerminate = false;
    private static final AtomicInteger messageIdCounter = new AtomicInteger(0);
    private HashMap<String, String> topics = new HashMap<>();
    private boolean isLoggedIn = false;
    private DatabaseService db;
    private String currentUsername = null;

    public StompMessagingProtocolImpl(DatabaseService db) {
        this.db = db;
    }

    @Override
    public void start(int connectionId, Connections<String> connections) {
        this.connectionId = connectionId;
        this.connections = connections;
    }

    @Override
    public void process(String message) {
        System.out.println("DEBUG: Received message:\n" + message);
        HashMap<String, String> headers = new HashMap<>();
        String[] arr = message.split("\\r?\\n");
        int i = 1;
        
        while(i < arr.length && !arr[i].isEmpty()){
            String[] pairs = arr[i].split(":",2); // 2 in case that a value contains ":" as well. 
            if (pairs.length == 2){
                headers.put(pairs[0], pairs[1]);
            }
            i++;
        }
        String body = "";
        if(i < arr.length){
            i++; 
            StringBuilder sb = new StringBuilder();
            while(i< arr.length){
                sb.append(arr[i] + "\n") ; 
                i++; 
            }
            body = sb.toString();
        }
        execute(arr[0], headers, body, message);
    }

    @Override
    public boolean shouldTerminate() {
        return shouldTerminate;
    }

    private void execute(String command, HashMap<String, String> headers, String body, String wholeMsg) { 
        if (!isLoggedIn && !command.equals("CONNECT")) {
            sendError(headers, wholeMsg, "Not connected");
            return;
        }
        switch (command) {
            case "CONNECT":
                handleConnect(headers, wholeMsg);
                break;
            case "SEND":
                handleSend(headers, body, wholeMsg); 
                break;
            case "SUBSCRIBE":
                handleSubscribe(headers, wholeMsg);
                break;
            case "UNSUBSCRIBE":
                handleUnsubscribe(headers, wholeMsg);
                break;
            case "DISCONNECT":
                handleDisconnect(headers, wholeMsg);
                break;
            default:
                sendError(headers, wholeMsg, "Error: Unknown Command");
                break;
        } 
    }

    private void handleConnect(HashMap<String, String> headers, String wholeMsg) {
        String version = headers.get("accept-version");
        String host = headers.get("host");
        String login = headers.get("login");
        String passcode = headers.get("passcode");

        if(version == null || !version.equals("1.2")){
            sendError(headers, wholeMsg, "Could not connect: Missing or invalid version (1.2 required)");
            return;
        }

        if (host == null || !host.equals("stomp.cs.bgu.ac.il")){
            sendError(headers, wholeMsg, "Could not connect: Missing or invalid host");
            return;
        }

        if (login == null || passcode == null) {
            sendError(headers, wholeMsg, "Could not connect: Missing login or passcode");
            return;
        }

        if (connections instanceof ConnectionsImpl) {
            ConnectionsImpl<String> connImpl = (ConnectionsImpl<String>) connections;
            if (connImpl.isUserActive(login)) {
                sendError(headers, wholeMsg, "User already logged in");
                return;
            }

            String queryUser = "SELECT password FROM users WHERE username='" + login + "'";
            String dbResult = db.execute(queryUser); 

            if (dbResult.startsWith("error")) {
                sendError(headers, wholeMsg, "Database error");
                return;
            }

            if (dbResult.isEmpty()) {
                String insertUser = "INSERT INTO users (username, password) VALUES ('" + login + "', '" + passcode + "')";
                String regResult = db.execute(insertUser);
                
                if (!regResult.equals("success")) {
                    sendError(headers, wholeMsg, "Registration failed");
                    return;
                }
            } else {
                String storedPass = dbResult.trim();
                if (!storedPass.equals(passcode)) {
                    sendError(headers, wholeMsg, "Wrong password");
                    return;
                }
            }
            connImpl.login(login, connectionId);

            long now = System.currentTimeMillis() / 1000;
            String logCmd = "INSERT INTO logins (username, login_time) VALUES ('" + login + "', " + now + ")";
            db.execute(logCmd);

            isLoggedIn = true;
            currentUsername = login;

            System.out.println("DEBUG: Connection authorized. Sending CONNECTED...");
            connections.send(connectionId, "CONNECTED\nversion:1.2\n\n");
        }
    }

    private void sendError(HashMap<String, String> headers, String wholeMsg, String errorDetails) {
        StringBuilder sb = new StringBuilder();
        sb.append("ERROR\n");
        
        if (headers.containsKey("receipt")) {
            sb.append("receipt-id:").append(headers.get("receipt")).append("\n");
        }
        
        sb.append("message: malformed frame received\n\n"); 
        
        sb.append("The message:\n");
        sb.append("-----\n");
        sb.append(wholeMsg);    
        sb.append("\n-----\n");
        sb.append(errorDetails).append("\n"); 

        connections.send(connectionId, sb.toString());
        if (connections instanceof ConnectionsImpl) {
            ((ConnectionsImpl<String>) connections).logout(connectionId);
        }
        shouldTerminate = true; 
        
    }

    private void handleSend(HashMap<String, String> headers, String body, String wholeMsg) {
        String dest = headers.get("destination");

        if(dest == null){
            sendError(headers, wholeMsg, "Did not contain a destination header,\n" + "which is REQUIRED for message propagation.");
            return;
        }

        if(!topics.containsKey(dest)){
            sendError(headers, wholeMsg, "User is not subscribed to topic " + dest);
            return;
        }

        long now = System.currentTimeMillis() / 1000;
        String safeBody = "report_event";

        String reportCmd = "INSERT INTO reports (username, file_name, time) VALUES ('" + 
                           currentUsername + "', '" + safeBody + "', " + now + ")";
        db.execute(reportCmd);

        String messageFrame = "message-id:" + messageIdCounter.incrementAndGet() + "\n" +
                              "destination:" + dest + "\n" +
                              "\n" +
                              body;

        connections.send(dest, messageFrame); 

        if (headers.containsKey("receipt")) {
            String receiptId = headers.get("receipt");
            String receiptMsg = "RECEIPT\n" +
                                "receipt-id:" + receiptId + "\n" +
                                "\n";
            connections.send(connectionId, receiptMsg);
        }
    }
    
    private void handleSubscribe(HashMap<String, String> headers, String wholeMsg) {
        String dest = headers.get("destination");

        if(dest == null){
            sendError(headers, wholeMsg, "Missing a destination header");
            return;
        }

        String id = headers.get("id");

        if(id == null){
            sendError(headers, wholeMsg, "Missing an id header");
            return;
        }
        int subId;
        try {
            subId = Integer.parseInt(id);
        } 
        catch (NumberFormatException e) {
            sendError(headers, wholeMsg, "Id must be a number");
            return;
        }

        if (connections instanceof ConnectionsImpl) {
            ((ConnectionsImpl<String>) connections).subscribe(dest, connectionId, subId);
        }
        
        topics.put(dest, id);

        if (headers.containsKey("receipt")) {
            String receiptId = headers.get("receipt");
            String receiptMsg = "RECEIPT\n" +
                                "receipt-id:" + receiptId + "\n" +
                                "\n";
            connections.send(connectionId, receiptMsg);
        }  
    }

    private void handleUnsubscribe(HashMap<String, String> headers, String wholeMsg) {
        String id = headers.get("id");

        if (id == null) {
            sendError(headers, wholeMsg, "Missing an id header");
            return;
        }

        int subId;
        try {
            subId = Integer.parseInt(id);
        }
        catch (NumberFormatException e) {
            sendError(headers, wholeMsg, "Id must be a number");
            return;
        }

        String topicToRemove = null;
        for (Map.Entry<String, String> entry : topics.entrySet()) {
            if (entry.getValue().equals(id)) {
                topicToRemove = entry.getKey();
                break;
            }
        }

        if (topicToRemove != null) {
            topics.remove(topicToRemove);

            if (connections instanceof ConnectionsImpl) {
                ((ConnectionsImpl<String>) connections).unsubscribe(topicToRemove, connectionId);
            }

            if (headers.containsKey("receipt")) {
                String receiptId = headers.get("receipt");
                String receiptMsg = "RECEIPT\n" +
                                    "receipt-id:" + receiptId + "\n" +
                                    "\n";
                connections.send(connectionId, receiptMsg);
            }
        }
        else {
            sendError(headers, wholeMsg, "No subscription found with id " + subId);
        }
    }

    private void handleDisconnect(HashMap<String, String> headers, String wholeMsg) {
        String receipt = headers.get("receipt");

        if(receipt == null){
            sendError(headers, wholeMsg, "Missing a receipt header");
            return;
        }

        if (currentUsername != null) {
            long now = System.currentTimeMillis() / 1000;
            String updateCmd = "UPDATE logins SET logout_time=" + now + 
                               " WHERE username='" + currentUsername + "' AND logout_time IS NULL";
            db.execute(updateCmd);
        }

        String receiptId = headers.get("receipt");
        String receiptMsg = "RECEIPT\n" +
                            "receipt-id:" + receiptId + "\n" +
                            "\n";
        connections.send(connectionId, receiptMsg);
        
        if (connections instanceof ConnectionsImpl) {
            connections.disconnect(connectionId);
        }

        shouldTerminate = true;
    }

}
