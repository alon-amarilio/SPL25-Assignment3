package bgu.spl.net.impl.stomp;
import java.util.concurrent.ConcurrentHashMap;

import bgu.spl.net.srv.Connections;
import bgu.spl.net.srv.ConnectionHandler;

public class ConnectionsImpl<T> implements Connections<T>{

    private final ConcurrentHashMap<Integer, ConnectionHandler<T>> clientHandlers = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String, ConcurrentHashMap<Integer, Integer>> channelSubscribers = new ConcurrentHashMap<>();


    @Override
    public boolean send(int connectionId, T msg) {
        ConnectionHandler<T> conn = clientHandlers.get(connectionId);
        if(conn != null){
            conn.send(msg);
            return true;
        }
        return false;
    }

    @Override
    public void send(String channel, T msg) {
        ConcurrentHashMap<Integer, Integer> subs = channelSubscribers.get(channel);
        if(subs != null){
            for(Integer connId : subs.keySet()){
                send(connId, msg);
            }
        }
    }

    @Override
    public void disconnect(int connectionId) {
        clientHandlers.remove(connectionId);
        for(ConcurrentHashMap<Integer, Integer> subs : channelSubscribers.values()){
            subs.remove(connectionId);
        }
    }

    public void connect(int connectId, ConnectionHandler<T> handler){
        clientHandlers.put(connectId, handler);
    }
    

    public void subscribe(String channel, int connectionId, int subscriptionId) {
        channelSubscribers.computeIfAbsent(channel, k -> new ConcurrentHashMap<>()).put(connectionId, subscriptionId);
    }

    public void unsubscribe(String channel, int connectionId) {
        ConcurrentHashMap<Integer, Integer> sub = channelSubscribers.get(channel);
        if(sub != null){
            sub.remove(connectionId);
        }
    }

    public Integer getSubscriptionId(String channel, int connectionId) {
        ConcurrentHashMap<Integer, Integer> sub = channelSubscribers.get(channel);
        if(sub != null){
            return sub.get(connectionId);
        }
        return null;
    }

    public java.util.Set<Integer> getSubscribers(String channel) {
        ConcurrentHashMap<Integer, Integer> subscribers = channelSubscribers.get(channel);
        if (subscribers != null) {
            return subscribers.keySet(); // מחזיר קבוצה של connectionId
        }
        return null;
    }
}


