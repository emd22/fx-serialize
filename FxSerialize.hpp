#pragma once

#include "FxTypes.hpp"
#include "FxHash.hpp"

#include <vector>
#include <cstdio>
#include <cassert>
#include <iostream>


#ifdef FX_USE_MEMPOOL
#include "FxMemPool.hpp"
#define FX_ALLOC_MEM(type_, size_) FxMemPool::Alloc<type_>(size_)
#define FX_FREE_MEM(ptr_) FxMemPool::Free(ptr_)
#else
#include <cstdlib>
#define FX_ALLOC_MEM(type_, size_) static_cast<type_*>(malloc(size_))
#define FX_FREE_MEM(ptr_) free(ptr_)
#endif

/*
*    Structure of FXSD (FoXtrot Serialized Data)
*
*       - The first section is a "Types" section that stores IDs and sizes for
*         all types that have been serialized. This includes primitives(int, char),
*         and members structures.
*
*       - The main "Data" section immediately follows the types and contains an entry
*         per serialized value. Member structures will be serialized and written inline
*         and will be treated like another entry inside of the current one.
*
    +-------------- File Header -------------------------------------------+
    | FXSD       | int8[4] | File signature, start of types section
    | 0000 0000  | uint32  | Length of types section
    +----------------------------------------------------------------------+

    +-------------- Type Entry --------------------------------------------+
    | EF         | uint8   | Entry start
    | 0000       | uint16  | Type ID
    | 0000       | uint16  | Size of type in bytes
    | 00         | uint8   | Number of child types (members in a struct)
    | 0000       | uint16  | Type ID of a child type
    |
    | ... Remaining child types ...
    |
    | BE         | uint8   | Entry end
    +----------------------------------------------------------------------+

    +-------------- Type Entry --------------------------------------------+
    | ...                                                                  |

    ... Remaining Type Entries ...

    +-------------- Data Section Header -----------------------------------+
    | .DAT       | int8[4] | Start of data section
    | 0000 0000  | uint32  | Length of data section
    +----------------------------------------------------------------------+

    +-------------- Data Entry --------------------------------------------+
    | 0B         | uint8   | Data entry start
    | 0000       | uint16  | Type ID
    | 0000 0000  | uint32  | Name Hash (name checks are disabled if zero)
    |
    | ... Data for all members ...
    |
    | B0         | uint8   | Data entry end
    +----------------------------------------------------------------------+

    +-------------- Data Entry --------------------------------------------+
    | ...                                                                  |

    ... Remaining Data Entries ...
*/


uint16* GetSerializeTypeIdCount();


class FxSerializeUtil
{
public:
    static inline uint16 SerializeTypeIdCount = 0;

    /** Generates a unique ID for a type `T` */
    template <typename T>
    static const uint16 GetTypeId()
    {
        return GetTypeId_<std::remove_const_t<std::remove_reference_t<T>>>();
    }

    template <typename T>
    static const uint16 GetTypeId_()
    {
        static uint16 tid = ++SerializeTypeIdCount;
        return tid;
    }

private:
};


struct FxSerializerIO;

template <typename T>
concept C_IsSerializable = requires(T t, FxSerializerIO& writer)
{
    { t.WriteTo(0, writer) };
};

class FxSerializerBaseSection
{
public:
    void Create(uint32 buffer_size);

    ~FxSerializerBaseSection()
    {
        FX_FREE_MEM(Data);
    }

    ////////////////////////
    // Write functions
    ////////////////////////

    inline void Write8(uint8 value)
    {
        assert(Index < Size);
        Data[Index++] = value;
    }

    inline void Write16(uint16 value16)
    {
        assert(Index + 2 <= Size);
        Data[Index++] = static_cast<uint8>((value16 >> 8));
        Data[Index++] = static_cast<uint8>(value16);
    }

    inline void Write32(uint32 value32)
    {
        Write16(static_cast<uint16>(value32 >> 16));
        Write16(static_cast<uint16>(value32));
    }

    /** Writes a buffer of bytes to the section */
    inline void WriteBuffer(uint32 size, const uint8* data)
    {
        assert(Index + size <= Size);

        memcpy(Data + Index, data, size);
        Index += size;
    }

    ////////////////////////
    // Read functions
    ////////////////////////

    uint8 Read8()
    {
        assert(Index + 1 <= Size);

        return Data[(Index)++];
    }

    uint16 Read16()
    {
        assert(Index + 2 <= Size);

        const uint16 hi = (static_cast<uint16>(Data[Index++])) << 8;
        const uint16 lo = Data[Index++];

        return hi | lo;
    }

    uint32 Read32()
    {
        return (static_cast<uint32>(Read16()) << 16 | static_cast<uint32>(Read16()));
    }


public:
    uint8* Data = nullptr;
    uint32 Index = 0;
    uint32 Size = 0;
};

struct FxSerializerDataSection : public FxSerializerBaseSection
{
    /// Data section start identifier
    static const uint8 DataIdentHeader = 0x0B;

    /// Data section end identifier
    static const uint8 DataIdentFooter = 0xB0;

    void WriteHeader(uint16 type_id, FxHash name_hash)
    {
        Write8(DataIdentHeader);

        Write16(type_id);
        Write32(name_hash);
    }

    void WriteFooter()
    {
        Write8(DataIdentFooter);
    }

    void PrintFormattedData(uint32 count)
    {
        const int width = 20;

        for (uint32 i = 0; i < count; i++) {
            uint8 value = Data[i];

            if (value == DataIdentHeader) {
                printf("<< ");
                continue;
            }
            else if (value == DataIdentFooter) {
                printf(">> ");
                continue;
            }

            if (!(i % width)) {
                puts("");
            }
            printf("%02X ", Data[i]);
        }

        puts("");
    }
};


struct FxSerializedType
{
    uint16 Id;
    uint16 Size;
    std::vector<FxSerializedType> Members;
};

class FxSerializerTypeSection : public FxSerializerBaseSection
{
    struct TypeEntry
    {
        uint16 Id;
        uint32 Offset;
    };

    /// Type section start identifier
    static const uint8 TypeIdentHeader = 0xEF;

    /// Type section end identifier
    static const uint8 TypeIdentFooter = 0xBE;

public:
    template <typename T>
    void WriteTypeForTypeId(FxSerializerIO& writer)
    {
        const uint16 type_id = FxSerializeUtil::GetTypeId<T>();

        if (IsTypePreviouslyWritten(type_id)) {
            return;
        }

        if constexpr (C_IsSerializable<T>) {
            T t_instance{ };
            t_instance.WriteTypeTo(writer);
        }
        else {
            WriteTypeWithoutChecks(type_id, sizeof(T));
        }

    }


    template <typename... Types>
    void WriteTypeWithoutChecks(uint16 type_id, uint16 type_size, Types&&... args)
    {
        if (IsTypePreviouslyWritten(type_id)) {
            return;
        }

        printf("Writing Type %d\n", type_id);
        const uint32 start_offset = Index;

        // Write start identifier
        Write8(TypeIdentHeader);

        // Write the type id
        Write16(type_id);

        // Write the size of the type
        Write16(type_size);

        // Number of member primitives
        Write8(sizeof...(args));

        auto write_member_func = [&] (uint16 type_id, uint16 size) {
            Write16(size);
            Write16(type_id);
        };

        // Write all of the types we reference (each type id)
        (write_member_func(FxSerializeUtil::GetTypeId<decltype(args)>(), sizeof(decltype(args))), ...);

        // Write end
        Write8(TypeIdentFooter);

        mRegisteredTypeIds.emplace_back(TypeEntry{ type_id, start_offset });
    }

    /** Writes out and each member inside it to the type section. */
    template <typename... Types>
    void WriteTypeAndMembers(FxSerializerIO& writer, uint16 type_id, uint16 type_size, Types&&... args)
    {
        if (IsTypePreviouslyWritten(type_id)) {
            return;
        }

        (WriteTypeForTypeId<std::remove_reference_t<decltype(args)>>(writer), ...);

        WriteTypeWithoutChecks(type_id, type_size, std::forward<Types>(args)...);
    }

    FxSerializedType ReadType(uint32 index);

    void PrintAllTypes()
    {
        printf("\n=== Types(%zu) ===\n", mRegisteredTypeIds.size());
        for (TypeEntry& entry : mRegisteredTypeIds) {
            PrintType(entry.Offset);
        }
    }

    void PrintType(uint32 index)
    {
        Index = index;

        const uint8 start_sanity = Read8();
        if (start_sanity != TypeIdentHeader) {
            printf("Start sanity is incorrect! %X != %X\n", start_sanity, TypeIdentHeader);
            return;
        }

        printf("Type (Sz=%d, Type=%d)\n", Read16(), Read16());

        const uint8 num_members = Read8();

        for (int i = 0; i < num_members; i++) {
            const uint16 member_size = Read16();
            const uint16 member_type_id = Read16();
            printf("\tMember Type ID: %d (size: %d)\n", member_type_id, member_size);
        }

        const uint8 end_sanity = Read8();
        if (end_sanity != TypeIdentFooter) {
            printf("End sanity is incorrect! %x != %x\n", end_sanity, TypeIdentFooter);
            return;
        }
    }

    void PrintFormattedData(uint32 count)
    {
        const int width = 20;

        for (uint32 i = 0; i < count; i++) {
            uint8 value = Data[i];

            if (value == TypeIdentHeader) {
                printf("<< ");
                continue;
            }
            else if (value == TypeIdentFooter) {
                printf(">> ");
                continue;
            }

            if (!(i % width)) {
                puts("");
            }
            printf("%02X ", Data[i]);
        }
        puts("");
    }

    uint32 FindIndexFromTypeId(uint16 id);

private:
    bool IsTypePreviouslyWritten(uint16 type_id);

private:
    std::vector<TypeEntry> mRegisteredTypeIds;
};

///////////////////////////////
// Serializer Input/Output
///////////////////////////////

// Multichars, easy to compare as we just need to compare the signatures as uint32s.
#define FX_SERIALIZER_IO_FILE_SIGNATURE 'FXSD'
#define FX_SERIALIZER_IO_SECTION_DATA_SIGNATURE '.DAT'

class FxSerializerIO
{
public:
    FxSerializerIO(uint32 buffer_size=10'000)
    {
        TypeSection.Create(buffer_size);
        DataSection.Create(buffer_size);
    }

    void PrintReadableEntry(uint32 start_index);

    /** Writes all sections of the serialized data to a file */
    void WriteToFile(const char* filename);

    /** Reads serialized data from a file into memory */
    void ReadFromFile(const char* filename);

private:
    void PrintBinaryValue(uint8 value)
    {
        printf("%02X ", value);
    }
    void PrintBinaryValue(uint16 value)
    {
        PrintBinaryValue(static_cast<uint8>(value >> 8));
        PrintBinaryValue(static_cast<uint8>(value));
    }
    void PrintBinaryValue(uint32 value)
    {
        PrintBinaryValue(static_cast<uint16>(value >> 16));
        PrintBinaryValue(static_cast<uint16>(value));
    }

public:
    FxSerializerTypeSection TypeSection;
    FxSerializerDataSection DataSection;
};

/////////////////////////////////
// Serializer Functions
/////////////////////////////////

// Base FxSerializeValue and FxDeserializeValue implementations, specializations are located in .cpp file
template <typename T>
void FxSerializeValue(FxSerializerIO& writer, const T& value)
{
    std::cout << "Type " << typeid(T).name() << " is not serializable!\n";
}

template <typename T>
void FxDeserializeValue(FxSerializerIO& writer, T dest)
{
    std::cout << "Type " << typeid(T).name() << " is not deserializable!\n";
}


template <typename... Types>
constexpr void FxSerializeStruct(FxSerializerIO& writer, uint16 type_id, FxHash name_hash, Types... members)
{
    FxSerializerDataSection& data = writer.DataSection;
    data.WriteHeader(type_id, name_hash);
    (FxSerializeValue<decltype(members)>(writer, members), ...);
    data.WriteFooter();
}


/**
 * Serializes a structure and writes to the SerializerIO `writer`.
 */
template <typename T> requires C_IsSerializable<T>
void FxSerializeValue(FxSerializerIO& writer, const T& value)
{
    value.WriteTo(0, writer);
}


/** Deserializes an object (struct) */
template <typename T> requires C_IsSerializable<T>
void FxDeserializeValue(FxSerializerIO& writer, T* value)
{
    value->ReadFrom(0, writer);
}

template <typename Type>
// Note that std::remove_cvref_t won't work here, this order is important!
using T_ExtractBarePtrType = std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<Type>>>*;

template <typename... Types>
constexpr void FxDeserializeStruct(FxSerializerIO& writer, FxHash name_hash, std::tuple<Types...> members)
{
    FxSerializerDataSection& data = writer.DataSection;

    uint8 temp;
    temp = data.Read8();
    if (temp != FxSerializerDataSection::DataIdentHeader) {
        printf("Header is incorrect! %02X != %02X\n", temp, FxSerializerDataSection::DataIdentHeader);
        return;
    }

    uint16 type_id = data.Read16();

    uint32 struct_hash = data.Read32();
    if (struct_hash && struct_hash != name_hash) {
        printf("Name hashes are not equal!\n");
        return;
    }

    //(FxDeserializeValue<decltype(members)>(writer, members), ...);
    std::apply(
        [&writer](auto&&... v)
        {
            (FxDeserializeValue(writer, const_cast<T_ExtractBarePtrType<decltype(v)>>(v)), ...);
        },
        members
    );

    temp = data.Read8();
    if (temp != FxSerializerDataSection::DataIdentFooter) {
        printf("Footer is incorrect!\n");
        return;
    }
}


/**
 * Returns a tuple containing a pointer to all `args` passed in.
 * For example:
 * int x; float y;
 * FxValuesToPtrsTuple(x, y) -> std::tuple<int*, float*>();
 */
template <typename... Types>
auto FxValuesToPtrsTuple(Types&... args)
{
    return std::tuple<Types*...>{ &args... };
}

#define FX_SERIALIZABLE_MEMBERS(...) \
    const uint16 SerializerTypeId_ = FxSerializeUtil::GetTypeId<decltype(*this)>(); \
    void WriteTypeTo(FxSerializerIO& writer) const \
    { \
        writer.TypeSection.WriteTypeAndMembers(writer, SerializerTypeId_, sizeof(decltype(*this)), __VA_ARGS__); \
    } \
    void WriteTo(FxHash name_hash, FxSerializerIO& writer) const \
    { \
        WriteTypeTo(writer); \
        FxSerializeStruct(writer, SerializerTypeId_, name_hash, __VA_ARGS__); \
    } \
    void ReadFrom(FxHash name_hash, FxSerializerIO& writer) const \
    { \
        FxDeserializeStruct(writer, name_hash, FxValuesToPtrsTuple(__VA_ARGS__)); \
    }
