

#include <cstdio>

#include "FxSerialize.hpp"

#include "FxTypes.hpp"
#include "FxHash.hpp"


struct TestStructB
{
    int32 A = 5;
    int32 B = 10;

    //std::string Str;

    FX_SERIALIZABLE_MEMBERS(A, B);
};

struct TestStructA
{
    int32 X = 30;
    int32 Y = 15;
    float32 Z = 3;

    std::string HW = "Hello, World";
    bool ch = false;

    TestStructB Other;

    // Serializes X, Y, Z, and then serializes the `other` struct
    FX_SERIALIZABLE_MEMBERS(X, Y, Z, Other, HW, ch);
};

struct TestStructC
{
    int32 Value;

    FX_SERIALIZABLE_MEMBERS(Value);
};

int main()
{
#ifdef FX_USE_MEMPOOL
    FxMemPool::GetGlobalPool().Create(1000);
#endif

    {
        FxSerializerIO test_writer;

        TestStructA data { 7, 3 };
        data.WriteTo(FxHashStr("TestStructA"), test_writer);

        TestStructC data2{100};
        data2.WriteTo(FxHashStr("TestStructC"), test_writer);

        test_writer.WriteToFile("Test.fxsd");
    }

    printf("\nReading serialized values...\n");

    {
        FxSerializerIO test_reader;

        test_reader.ReadFromFile("Test.fxsd");

        TestStructA data{};
        data.ReadFrom(FxHashStr("TestStructA"), test_reader);

        TestStructC data2{};
        data2.ReadFrom(FxHashStr("TestStructC"), test_reader);

        printf("Data2: %d\n", data2.Value);
        printf("Values: {%d, %d, %f}, other.B = %d\n", data.X, data.Y, data.Z, data.Other.B);
        std::cout << "Str: " << data.HW << "\n";
    }

    return 0;
}
