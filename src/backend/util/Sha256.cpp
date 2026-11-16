#include "snes/util/Sha256.hpp"
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace snes {
namespace {

u32 rotateRight(u32 value, unsigned bits) {
    return (value >> bits) | (value << (32u - bits));
}

const u32 RoundConstants[64] = {
    0x428A2F98u,0x71374491u,0xB5C0FBCFu,0xE9B5DBA5u,0x3956C25Bu,0x59F111F1u,0x923F82A4u,0xAB1C5ED5u,
    0xD807AA98u,0x12835B01u,0x243185BEu,0x550C7DC3u,0x72BE5D74u,0x80DEB1FEu,0x9BDC06A7u,0xC19BF174u,
    0xE49B69C1u,0xEFBE4786u,0x0FC19DC6u,0x240CA1CCu,0x2DE92C6Fu,0x4A7484AAu,0x5CB0A9DCu,0x76F988DAu,
    0x983E5152u,0xA831C66Du,0xB00327C8u,0xBF597FC7u,0xC6E00BF3u,0xD5A79147u,0x06CA6351u,0x14292967u,
    0x27B70A85u,0x2E1B2138u,0x4D2C6DFCu,0x53380D13u,0x650A7354u,0x766A0ABBu,0x81C2C92Eu,0x92722C85u,
    0xA2BFE8A1u,0xA81A664Bu,0xC24B8B70u,0xC76C51A3u,0xD192E819u,0xD6990624u,0xF40E3585u,0x106AA070u,
    0x19A4C116u,0x1E376C08u,0x2748774Cu,0x34B0BCB5u,0x391C0CB3u,0x4ED8AA4Au,0x5B9CCA4Fu,0x682E6FF3u,
    0x748F82EEu,0x78A5636Fu,0x84C87814u,0x8CC70208u,0x90BEFFFAu,0xA4506CEBu,0xBEF9A3F7u,0xC67178F2u
};

} // namespace

Sha256::Sha256() : state_(), buffer_(), buffered_(0), totalBytes_(0), finalized_(false) {
    state_[0]=0x6A09E667u; state_[1]=0xBB67AE85u; state_[2]=0x3C6EF372u; state_[3]=0xA54FF53Au;
    state_[4]=0x510E527Fu; state_[5]=0x9B05688Cu; state_[6]=0x1F83D9ABu; state_[7]=0x5BE0CD19u;
    std::memset(buffer_, 0, sizeof(buffer_));
}

void Sha256::transform(const u8 block[64]) {
    u32 words[64];
    for (unsigned i=0;i<16u;++i) {
        const unsigned p=i*4u;
        words[i]=(static_cast<u32>(block[p])<<24)|(static_cast<u32>(block[p+1])<<16)|
                 (static_cast<u32>(block[p+2])<<8)|block[p+3];
    }
    for (unsigned i=16u;i<64u;++i) {
        const u32 s0=rotateRight(words[i-15u],7)^rotateRight(words[i-15u],18)^(words[i-15u]>>3);
        const u32 s1=rotateRight(words[i-2u],17)^rotateRight(words[i-2u],19)^(words[i-2u]>>10);
        words[i]=words[i-16u]+s0+words[i-7u]+s1;
    }
    u32 a=state_[0],b=state_[1],c=state_[2],d=state_[3];
    u32 e=state_[4],f=state_[5],g=state_[6],h=state_[7];
    for (unsigned i=0;i<64u;++i) {
        const u32 s1=rotateRight(e,6)^rotateRight(e,11)^rotateRight(e,25);
        const u32 choose=(e&f)^((~e)&g);
        const u32 t1=h+s1+choose+RoundConstants[i]+words[i];
        const u32 s0=rotateRight(a,2)^rotateRight(a,13)^rotateRight(a,22);
        const u32 majority=(a&b)^(a&c)^(b&c);
        const u32 t2=s0+majority;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
    state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
}

void Sha256::update(const void* data, std::size_t size) {
    if (finalized_ || !data || size==0u) return;
    const u8* bytes=static_cast<const u8*>(data);
    totalBytes_+=static_cast<u64>(size);
    while (size!=0u) {
        const std::size_t take=(size < 64u-buffered_) ? size : 64u-buffered_;
        std::memcpy(buffer_+buffered_,bytes,take);
        buffered_+=take; bytes+=take; size-=take;
        if (buffered_==64u) { transform(buffer_); buffered_=0; }
    }
}

std::string Sha256::finalHex() {
    if (!finalized_) {
        const u64 bits=totalBytes_*8u;
        buffer_[buffered_++]=0x80u;
        if (buffered_>56u) {
            while(buffered_<64u) buffer_[buffered_++]=0;
            transform(buffer_); buffered_=0;
        }
        while(buffered_<56u) buffer_[buffered_++]=0;
        for(unsigned i=0;i<8u;++i)
            buffer_[56u+i]=static_cast<u8>(bits>>(56u-i*8u));
        transform(buffer_);
        buffered_=0;
        finalized_=true;
    }
    std::ostringstream out;
    out<<std::hex<<std::setfill('0');
    for(unsigned i=0;i<8u;++i) out<<std::setw(8)<<state_[i];
    return out.str();
}

std::string Sha256::digest(const void* data,std::size_t size) {
    Sha256 hash; hash.update(data,size); return hash.finalHex();
}

std::string Sha256::digest(const std::vector<u8>& data) {
    return digest(data.empty()?0:&data[0],data.size());
}

bool Sha256::digestFile(const std::string& path,std::string& hex,std::string& error) {
    error.clear();
    std::ifstream input(path.c_str(),std::ios::binary);
    if(!input){error="Could not open file for SHA-256: "+path;return false;}
    Sha256 hash; char buffer[65536];
    while(input){input.read(buffer,sizeof(buffer));const std::streamsize n=input.gcount();if(n>0)hash.update(buffer,static_cast<std::size_t>(n));}
    if(!input.eof()){error="Could not read complete file for SHA-256: "+path;return false;}
    hex=hash.finalHex();return true;
}

} // namespace snes
