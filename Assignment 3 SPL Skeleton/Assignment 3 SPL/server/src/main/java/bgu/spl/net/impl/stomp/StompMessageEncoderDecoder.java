package bgu.spl.net.impl.stomp;

import bgu.spl.net.api.MessageEncoderDecoder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public class StompMessageEncoderDecoder implements MessageEncoderDecoder<String>{

    private byte[] bytes = new byte[1 << 10]; //1 KB
    private int len = 0;

    @Override
    public String decodeNextByte(byte nextByte) {
        if(nextByte == '\0'){
            return getStringFromBytes();
        }
        addByte(nextByte);
        return null;
    }

    @Override
    public byte[] encode(String message) {
        return (message + '\0').getBytes(StandardCharsets.UTF_8);
    }
    
    private void addByte(byte nextByte){
        if(len >= bytes.length){
            bytes = Arrays.copyOf(bytes, len * 2);
        }
        bytes[len] = nextByte;
        len++;
    }

    private String getStringFromBytes(){
        String result = new String(bytes, 0, len, StandardCharsets.UTF_8);
        len = 0;
        return result;
    }
    
}
