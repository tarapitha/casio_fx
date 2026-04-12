```mermaid
sequenceDiagram
    participant T as Transmitter
    participant R as Receiver

    Note over T,R: Initial handshake
    T->>R: SYN (0x16)
    R-->>T: DC3 (0x13)
    
    rect rgb(100, 100, 100)
    Note right of T: Header section
    T->>R: ':' (0x3A)
    T->>R: 38 payload bytes
    T->>R: CHKSUM (mod 256)
    end

    Note over T,R: Response
    alt Success
        R-->>T: ACK (0x06)

        opt Depending on Header metadata
            loop Until specified count or terminating header
                rect rgb(80, 80, 80)
                Note right of T: Data section
                T->>R: ':' (0x3A)
                T->>R: Variable length payload
                T->>R: CHKSUM (mod 256)
                end
                alt Success
                    R-->>T: ACK (0x06)
                else Incorrect checksum
                    R-->>T: CHKSUM_ERROR (0x2B)
                    T-xR: Connection closed
                else Unspecified error
                    R-->>T: GENERIC_ERROR (0x22)
                    T-xR: Connection closed
                end
            end
        end

    else Data area already used<br/>(Handshake)
        rect rgb(40, 40, 40)
        R-->>T: AREA_USED (0x21)
        Note left of R: Receiver is waiting<br/>for a response
        
        alt Overwrite
            T->>R: ACK (0x06)
            Note over R: Continue

            loop Until specified count or terminating header
                rect rgb(80, 80, 80)
                Note right of T: Data section
                T->>R: ':' (0x3A)
                T->>R: Variable length payload
                T->>R: CHKSUM (mod 256)
                end
                alt Success
                    R-->>T: ACK (0x06)
                else Incorrect checksum
                    R-->>T: CHKSUM_ERROR (0x2B)
                    T-xR: Connection closed
                else Unspecified error
                    R-->>T: GENERIC_ERROR (0x22)
                    T-xR: Connection closed
                end
            end

        else Decline
            T->>R: NAK (0x15)
            Note over R: Cancel
            T-xR: Connection closed
        end
        end
    else Incorrect checksum
        R-->>T: CHKSUM_ERROR (0x2B)
        T-xR: Connection closed
    else Not enough free space
        R-->>T: MEM_FULL (0x24)
        T-xR: Connection closed
    else Unspecified error
        R-->>T: GENERIC_ERROR (0x22)
        T-xR: Connection closed
    end
```
