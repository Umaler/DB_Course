#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <nlohmann/json.hpp>
#include <atomic>
#include <array>
#include <mutex>
#include <thread>
#include <list>
#include <SFML/Network.hpp>
#include <iostream>

extern "C" {
#include "csi_fun.h"
}

template <typename K, typename V>
V getDefault(nlohmann::json js, K key, V defaultV) {
    if(js[key].is_null())
        return defaultV;
    else
        return js[key];
}

class HandlerBase {
friend class HandlersList;
public:
    //real and imag (ampl and phase)
    //recv
    //trans
    //subcarrier
    typedef std::pair<std::vector<std::vector<std::vector<double>>>,
                      std::vector<std::vector<std::vector<double>>>> datatype;

    virtual Glib::ustring getName() const = 0;

    virtual void set_settings(nlohmann::json config) = 0;

};

class ReceiverHandler : public HandlerBase {
public:
    virtual std::optional<HandlerBase::datatype> tryCollect() {
        return HandlerBase::datatype();
    }

    Glib::ustring getName() const override {
        return "Стандартный сервер";
    }

    virtual void set_pause(bool val=true) {
        paused = val;
    }

    virtual bool get_pause() const {
        return paused;
    }

    void set_settings(nlohmann::json config) override {}

protected:
    bool paused = true;

};

class RouterReceiver : public ReceiverHandler {
public:
    RouterReceiver() {
        socket.setBlocking(false);
    }

    RouterReceiver(nlohmann::json config) {
        socket.setBlocking(false);
        set_settings(config);
    }

    void set_settings(nlohmann::json config) override {
        port = getDefault(config, "port", port);
        recv_antennas = getDefault(config["receiver"], "antennas", recv_antennas);
        trans_antenntas = getDefault(config["transmiter"], "antennas", trans_antenntas);
        subcarriers = getDefault(config["receiver"], "subcarriers", subcarriers);

        if(!paused) {
            if (socket.bind(port) != sf::Socket::Status::Done) {
                std::cerr << "RouterReceiver: unable to bind socket to port " << port << std::endl;
                return;
            }
        }
        //set_pause(false);
    }

    void set_pause(bool val=true) override {
        ReceiverHandler::set_pause(val);
        if(val == true) {
            socket.unbind();
        }
        else if (socket.bind(port) != sf::Socket::Status::Done) {
            std::cerr << "RouterReceiver: unable to bind socket to port " << port << std::endl;
            return;
        }
    }

    std::optional<HandlerBase::datatype> tryCollect() {
        try {

            const size_t rawBufSize = sf::UdpSocket::MaxDatagramSize;    //maximal size of UDP datagram

            unsigned char                in[rawBufSize];
            std::size_t                  received = 0;
            sf::IpAddress                sender;
            unsigned short               senderPort;

            HandlerBase::datatype bufferToTransfer;

            sf::Socket::Status status = socket.receive(in, sizeof(in), received, sender, senderPort);
            if(status != sf::Socket::Status::Done) {
                return std::nullopt;
            }

            csi_struct csi_status;
            record_status(in, received, &csi_status);

            if(csi_status.payload_len < 1056) {
                return std::nullopt;
            }

            size_t nr = std::min(static_cast<uint8_t>(csi_status.nr), recv_antennas);
            size_t nc = std::min(static_cast<uint8_t>(csi_status.nc), trans_antenntas);
            size_t maxSubcars = std::min(static_cast<uint16_t>(csi_status.num_tones), subcarriers);

            std::vector<unsigned char> data_buf(csi_status.payload_len);
            COMPLEX csi_matrix[3][3][114];
            record_csi_payload(in, &csi_status, &data_buf[0], csi_matrix);

            bufferToTransfer.first.resize(nr);
            bufferToTransfer.second.resize(nr);
            for(size_t i = 0; i < nr; i++) {
                bufferToTransfer.first[i].resize(nc);
                bufferToTransfer.second[i].resize(nc);
                for(size_t j = 0; j < nc; j++) {
                    bufferToTransfer.first[i][j].resize(maxSubcars);
                    bufferToTransfer.second[i][j].resize(maxSubcars);
                    for(size_t sub_car = 0; sub_car < maxSubcars; sub_car++) {
                        bufferToTransfer.first[i][j][sub_car] = csi_matrix[i][j][sub_car].real;
                        bufferToTransfer.second[i][j][sub_car] = csi_matrix[i][j][sub_car].imag;
                    }
                }
            }
            return std::move(bufferToTransfer);
        }
        catch(std::exception& ex) {
            std::cerr << "RouterReceiver: " << ex.what() << std::endl;
        }
        catch(...) {
            std::cerr << "RouterReceiver: unknown error" << std::endl;
        }
        return std::nullopt;
    }

    Glib::ustring getName() const override {
        return "UDP сервер для роутеров";
    }

private:
    sf::UdpSocket socket;

    size_t port = 50000;
    uint8_t recv_antennas = 3;
    uint8_t trans_antenntas = 3;
    uint16_t subcarriers = 56;

};

class PreprocessingHandler : public HandlerBase {
public:
    Glib::ustring getName() const override {
        return "Стандартный предобработчик";
    }

    virtual std::optional<HandlerBase::datatype> process(const HandlerBase::datatype& toProcess) {
        return toProcess;
    }

    void set_settings(nlohmann::json config) override {}

};

class HandlersList {
public:

    static HandlersList& getInstance() {
        static HandlersList hl;
        return hl;
    }

    std::vector<Glib::ustring> getRecvNames() const {
        std::vector<Glib::ustring> names;
        names.reserve(receivers.size());
        for(const auto& i : receivers) {
            names.push_back(i->getName());
        }
        return names;
    }

    ReceiverHandler& getRecvHandler(size_t idx) {
        return *receivers.at(idx);
    }

    ReceiverHandler& getRecvHandler(Glib::ustring name) {
        return *receivers.at(recvNameToIdx.at(name));
    }

    std::vector<Glib::ustring> getPreprocNames() const {
        std::vector<Glib::ustring> names;
        names.reserve(preprocessor.size());
        for(const auto& i : preprocessor) {
            names.push_back(i->getName());
        }
        return names;
    }

    PreprocessingHandler& getPreprocHandler(size_t idx) {
        return *preprocessor.at(idx);
    }

    PreprocessingHandler& getPreprocHandler(Glib::ustring name) {
        return *preprocessor.at(preprocNameToIdx.at(name));
    }

    void pauseAll() {
        for(auto& i : receivers) {
            i->set_pause();
        }
    }

private:
    std::vector<std::unique_ptr<ReceiverHandler>> receivers;
    std::map<Glib::ustring, size_t> recvNameToIdx;

    std::vector<std::unique_ptr<PreprocessingHandler>> preprocessor;
    std::map<Glib::ustring, size_t> preprocNameToIdx;

    HandlersList()
    {
        receivers.emplace_back(new ReceiverHandler());
        receivers.emplace_back(new RouterReceiver());
        preprocessor.emplace_back(new PreprocessingHandler());

        for(size_t i = 0; i < receivers.size(); i++) {
            recvNameToIdx[receivers[i]->getName()] = i;
        }
        for(size_t i = 0; i < preprocessor.size(); i++) {
            preprocNameToIdx[preprocessor[i]->getName()] = i;
        }
    }

};
