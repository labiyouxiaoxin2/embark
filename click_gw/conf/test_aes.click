elementclass AESGateway {
    $rule | input 
    -> IPsecESPEncap
    -> IPsecAuthHMACSHA1(1)
    -> IPsecAES(1)
    -> IPsecEncap(50)
    -> output
}

//FromDump(FILENAME /project/cs/netsys/data/pcaps/m57/m57.pcap, STOP true) -> c;

pd0::PollDevice(p513p1, QUEUE 0, BURST 32) -> Strip(14)
    -> gw0::AESGateway
    -> EtherEncap(0x86DD, 1:1:1:1:1:1, 2:2:2:2:2:2)
    -> SimpleQueue(20000)
    -> sd0::SendDevice(p513p2, QUEUE 0, BURST 32);

pd1::PollDevice(p513p1, QUEUE 1, BURST 32) -> Strip(14)
    -> gw1::AESGateway
    -> EtherEncap(0x86DD, 1:1:1:1:1:1, 2:2:2:2:2:2)
    -> SimpleQueue(20000)
    -> sd1::SendDevice(p513p2, QUEUE 1, BURST 32);

pd2::PollDevice(p513p1, QUEUE 2, BURST 32) -> Strip(14)
    -> gw2::AESGateway
    -> EtherEncap(0x86DD, 1:1:1:1:1:1, 2:2:2:2:2:2)
    -> SimpleQueue(20000)
    -> sd2::SendDevice(p513p2, QUEUE 2, BURST 32);

pd3::PollDevice(p513p1, QUEUE 3, BURST 32) -> Strip(14)
    -> gw3::AESGateway
    -> EtherEncap(0x86DD, 1:1:1:1:1:1, 2:2:2:2:2:2)
    -> SimpleQueue(20000)
    -> sd3::SendDevice(p513p2, QUEUE 3, BURST 32);

StaticThreadSched(pd0 1, pd1 2, pd2 3, pd3 4);
StaticThreadSched(sd0 5, sd1 6, sd2 7, sd3 8);