package bgu.spl.net.impl.stomp;

import bgu.spl.net.srv.Server;
import bgu.spl.net.srv.DatabaseService;

public class StompServer {

    public static void main(String[] args) {

        DatabaseService dbService = new DatabaseService("127.0.0.1", 7778); 
        if (!dbService.connect()) {
            System.out.println("Warning: Could not connect to DB server!");
        }

        if (args.length < 2) {
            System.out.println("Usage: port server_type(tpc/reactor)");
            return;
        }

        int port = Integer.parseInt(args[0]);
        String type = args[1];

        if (type.equals("tpc")) {
            Server.threadPerClient(
                    port,
                    () -> new StompMessagingProtocolImpl(dbService), 
                    StompMessageEncoderDecoder::new
            ).serve();
        } else if (type.equals("reactor")) {
            Server.reactor(
                    Runtime.getRuntime().availableProcessors(),
                    port,
                    () -> new StompMessagingProtocolImpl(dbService), 
                    StompMessageEncoderDecoder::new
            ).serve();
        }
    }
}