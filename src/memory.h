
class Memory {
    public:
        const char DELIMITER = '~';
        const int PROPERTY_COUNT = 10;
        const unsigned int PROPERTY_VALUE_LENGTH = 100;

        Memory();
        ~Memory();

        const bool ready() const;
        void ready(const bool value);
        
        char* readNext();
        void write(const char* value);

        void commit();
        void clear();

    private:
        bool _ready = false;
        int _index = 1;
};