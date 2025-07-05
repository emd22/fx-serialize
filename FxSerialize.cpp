#include "FxSerialize.hpp"


#include "FxTypes.hpp"
#include "FxUtil.hpp"

#include <iostream>


#define REVERT_INDEX_AFTER_SCOPE \
    uint32 old_index_ = Index; \
    FxDefer([&] { Index = old_index_; })


static FILE* FxFileOpen(const char* filename, const char* mode)
{
#ifdef fopen_s
    FILE* fp = nullptr;
    fopen_s(&fp, filename, mode);
    return fp;
#endif

    return fopen(filename, mode);
}



static uint16 FxSerializeTypeIdCount = 1;

uint16* GetSerializeTypeIdCount()
{
    return &FxSerializeTypeIdCount;
}


void FxSerializerBaseSection::Create(uint32 buffer_size)
{
    Size = buffer_size;
    Index = 0;
    Data = FX_ALLOC_MEM(uint8, Size);
}

FxSerializedType FxSerializerTypeSection::ReadType(uint32 index)
{
    REVERT_INDEX_AFTER_SCOPE;
    Index = index;

    uint8 sanity_header = Read8();
    if (sanity_header != TypeIdentHeader) {
        printf("Sanity header does not match!\n");
        return {};
    }

    FxSerializedType type;
    type.Id = Read16();
    type.Size = Read16();

    uint8 number_of_members = Read8();

    for (int i = 0; i < number_of_members; i++) {
        Read16(); // Skip size of member

        uint16 member_id = Read16();
        uint32 member_index = FindIndexFromTypeId(member_id);

        // Push the member to the type
        FxSerializedType member = ReadType(member_index);
        type.Members.emplace_back(member);
    }

    uint8 sanity_footer = Read8();
    if (sanity_footer != TypeIdentFooter) {
        printf("Sanity footer does not match!\n");
    }

    return type;
}

bool FxSerializerTypeSection::IsTypePreviouslyWritten(uint16 type_id)
{
    for (TypeEntry& tp : mRegisteredTypeIds) {
        if (tp.Id == type_id) {
            // Type is already written, skip
            return true;
        }
    }
    return false;
}

uint32 FxSerializerTypeSection::FindIndexFromTypeId(uint16 id)
{
    REVERT_INDEX_AFTER_SCOPE;
    Index = 0;

    uint32 entry_index = 0;

    bool found_id = false;

    while (!found_id && Index < Size) {

        entry_index = Index;

        uint8 sanity_header = Read8();
        if (sanity_header != TypeIdentHeader) {
            printf("Sanity header error when searching for Type ID %04x (%02X != %02X)\n", id, sanity_header, TypeIdentHeader);
            break;
        }

        uint16 type_id = Read16();

        // We found the id, break to return the index for the start of the entry
        if (type_id == id) {
            break;
        }

        Read16(); // size

        uint8 number_of_members = Read8();
        for (int i = 0; i < number_of_members; i++) {
            Read16(); // size
            Read16(); // type id
        }

        uint8 sanity_footer = Read8();

        if (sanity_footer != TypeIdentFooter) {
            printf("Sanity footer error when searching for Type ID\n");
            break;
        }

        // Continue to next entry...
    }

    return entry_index;
}

///////////////////////////////
// Serializer Input/Output
///////////////////////////////


void FxSerializerIO::WriteToFile(const char* filename)
{
    FILE* fp = FxFileOpen(filename, "wb");
    if (fp == nullptr) {
        printf("Error opening file '%s' for writing!\n", filename);
        return;
    }

    uint32 signature = FX_SERIALIZER_IO_FILE_SIGNATURE;
    fwrite(&signature, sizeof(signature), 1, fp);

    // Write the size of the types section in bytes
    fwrite(&TypeSection.Index, sizeof(uint32), 1, fp);
    // Write the types section
    fwrite(TypeSection.Data, 1, TypeSection.Index, fp);

    uint32 data_signature = FX_SERIALIZER_IO_SECTION_DATA_SIGNATURE;
    fwrite(&data_signature, sizeof(data_signature), 1, fp);

    // Write the size of the data section in bytes
    fwrite(&DataSection.Index, sizeof(uint32), 1, fp);
    // Write the data section
    fwrite(DataSection.Data, 1, DataSection.Index, fp);

    fclose(fp);
}


void FxSerializerIO::ReadFromFile(const char* filename)
{
    FILE* fp = FxFileOpen(filename, "rb");
    if (fp == nullptr) {
        printf("Could not open file\n");
        return;
    }

    // Get the size of the file
    fseek(fp, 0, SEEK_END);
    unsigned long size = ftell(fp);
    rewind(fp);

    FxDefer([&fp] {
        fclose(fp);
    });

    {
        // Read in the file signature (expect "FXSD") as a uint32 to compare with our multichar value
        uint32 signature_buffer = 0;
        fread(&signature_buffer, sizeof(uint32), 1, fp);

        const uint32 expected_signature = FX_SERIALIZER_IO_FILE_SIGNATURE;

        if (signature_buffer != expected_signature) {
            printf("File signature is incorrect!\n");
            return;
        }

        // Read in the size of the types section
        uint32 size_of_types;
        fread(&size_of_types, sizeof(uint32), 1, fp);

        // Read in the types
        fread(TypeSection.Data, 1, size_of_types, fp);
    }
    {
        // Read in the data signature (expect ".DAT") as a uint32 to compare with our multichar value
        uint32 signature_buffer = 0;
        fread(&signature_buffer, sizeof(uint32), 1, fp);

        const uint32 expected_data_signature = FX_SERIALIZER_IO_SECTION_DATA_SIGNATURE;

        if (signature_buffer != expected_data_signature) {
            printf("File data signature is incorrect!\n");
            return;
        }

        uint32 size_of_data;
        fread(&size_of_data, sizeof(uint32), 1, fp);

        // Read in the data section
        fread(DataSection.Data, 1, size_of_data, fp);
    }
}

void FxSerializerIO::PrintReadableEntry(uint32 start_index)
{
    uint32 old_index = DataSection.Index;
    DataSection.Index = start_index;

    printf("\nMagic Start: "); PrintBinaryValue(DataSection.Read8());
    const uint16 type_id = DataSection.Read16();
    printf("\nType ID    : "); PrintBinaryValue(type_id);
    printf("\nName Hash  : "); PrintBinaryValue(DataSection.Read32());
    puts("");

    FxSerializedType entry_type = TypeSection.ReadType(TypeSection.FindIndexFromTypeId(type_id));

    printf("Type {Sz:%d, Members: %zu}\n", entry_type.Size, entry_type.Members.size());

    uint32 total_members_size = 0;

    for (FxSerializedType& member : entry_type.Members) {
        printf("Member(%d, Sz: %d)\n", member.Id, member.Size);
        total_members_size += member.Size;
    }
    printf("Total size of members: %u\n", total_members_size);
    DataSection.Index += total_members_size;

    printf("Magic End: "); PrintBinaryValue(DataSection.Read8());

    DataSection.Index = old_index;
}


/////////////////////////////////////
// FxSerializeValue specializations
/////////////////////////////////////


template <>
void FxSerializeValue(FxSerializerIO& writer, const int32& value)
{
    writer.DataSection.Write32(static_cast<uint32>(value));
}

template <>
void FxSerializeValue(FxSerializerIO& writer, const std::string& value)
{
    const uint32 str_size = value.size();

    // Write the size of the string
    writer.DataSection.Write16(str_size);
    writer.DataSection.WriteBuffer(str_size, reinterpret_cast<const uint8_t*>(value.c_str()));
}

template <>
void FxSerializeValue(FxSerializerIO& writer, const float32& value)
{
    writer.DataSection.Write32(static_cast<uint32>(value));
}

template <>
void FxDeserializeValue(FxSerializerIO& writer, int32* value)
{
    (*value) = writer.DataSection.Read32();
}

template <>
void FxDeserializeValue(FxSerializerIO& writer, float32* value)
{
    (*value) = writer.DataSection.Read32();
}
