#include "ear/direct_speakers/gain_calculator_direct_speakers.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include "ear/common/geom.hpp"
#include "ear/common/helpers/eigen_helpers.hpp"
#include "ear/common/point_source_panner.hpp"
#include "ear/common/screen_edge_lock.hpp"
#include "ear/direct_speakers/mapping_rules.hpp"
#include "ear/helpers/assert.hpp"
#include "ear/layout.hpp"
#include "ear/metadata.hpp"

namespace ear {

  namespace {

    // does a sequence contain a value?
    template <typename Seq, typename Value>
    bool contains(const Seq& seq, const Value& value) {
      return std::find(seq.begin(), seq.end(), value) != seq.end();
    }

    // does a rule apply to a given input layout (itu name), canonical
    // speakerLabel and output layout?
    bool rule_applies(const MappingRule& rule, const std::string& input_layout,
                      const std::string& speakerLabel,
                      const Layout& output_layout) {
      if (rule.input_layouts.size() &&
          !contains(rule.input_layouts, input_layout))
        return false;

      if (rule.output_layouts.size() &&
          !contains(rule.output_layouts, output_layout.name()))
        return false;

      if (speakerLabel != rule.speakerLabel) return false;

      for (auto& channel_gain : rule.gains)
        if (!contains(output_layout.channelNames(), channel_gain.first))
          return false;

      return true;
    }
  }  // namespace

  GainCalculatorDirectSpeakersImpl::GainCalculatorDirectSpeakersImpl(
      const Layout& layout,
      std::map<std::string, std::string> additionalSubstitutions)
      : _layout(layout),
        _pointSourcePanner(configurePolarPanner(_layout.withoutLfe())),
        _screenEdgeLockHandler(ScreenEdgeLockHandler(_layout.screen())),
        _nChannels(static_cast<int>(layout.channels().size())),
        _channelNames(layout.channelNames()) {
    std::vector<double> azimuths, elevations, distances;
    for (auto channel : layout.channels()) {
      PolarPosition pos = channel.polarPositionNominal();
      azimuths.push_back(pos.azimuth);
      elevations.push_back(pos.elevation);
      distances.push_back(pos.distance);
    }
    _azimuths = Eigen::Map<Eigen::VectorXd>(azimuths.data(), azimuths.size());
    _elevations =
        Eigen::Map<Eigen::VectorXd>(elevations.data(), elevations.size());
    _distances =
        Eigen::Map<Eigen::VectorXd>(distances.data(), distances.size());
    _positions = toPositionsMatrix(layout.positions());
    _isLfe = copy_vector<decltype(_isLfe)>(layout.isLfe());
    _substitutions = std::map<std::string, std::string>{
        {"LFE", "LFE1"}, {"LFEL", "LFE1"}, {"LFER", "LFE2"}};
    _substitutions.insert(additionalSubstitutions.begin(),
                          additionalSubstitutions.end());
  };

  SpeakerPosition GainCalculatorDirectSpeakersImpl::_applyScreenEdgeLock(
      SpeakerPosition position) {
    if (position.type() == typeid(PolarSpeakerPosition)) {
      PolarSpeakerPosition pos = boost::get<PolarSpeakerPosition>(position);
      std::tie(pos.azimuth, pos.elevation) =
          _screenEdgeLockHandler.handleAzimuthElevation(
              pos.azimuth, pos.elevation, pos.screenEdgeLock);
      return pos;
    } else {
      CartesianSpeakerPosition pos =
          boost::get<CartesianSpeakerPosition>(position);
      std::tie(pos.X, pos.Y, pos.Z) = _screenEdgeLockHandler.handleVector(
          toCartesianVector3d(pos), pos.screenEdgeLock);
      return pos;
    }
  }

  bool GainCalculatorDirectSpeakersImpl::_isLfeChannel(
      const DirectSpeakersTypeMetadata& metadata, const WarningCB& warning_cb) {
    bool has_lfe_freq = (metadata.channelFrequency.lowPass != boost::none &&
                         metadata.channelFrequency.lowPass.get() <= 200 &&
                         metadata.channelFrequency.highPass == boost::none);
    if (!has_lfe_freq && (metadata.channelFrequency.lowPass ||
                          metadata.channelFrequency.highPass))
      warning_cb({Warning::Code::FREQ_NOT_LFE,
                  "frequency indication present but does "
                  "not indicate an LFE channel"});

    bool has_lfe_name = false;
    for (auto speakerLabel : metadata.speakerLabels) {
      std::string nominalLabel = _nominalSpeakerLabel(speakerLabel);
      if (nominalLabel == std::string("LFE1") ||
          nominalLabel == std::string("LFE2")) {
        has_lfe_name = true;
      }
    }

    if (has_lfe_freq != has_lfe_name && metadata.speakerLabels.size())
      warning_cb({Warning::Code::FREQ_SPEAKERLABEL_LFE_MISMATCH,
                  "LFE indication from frequency element does not match "
                  "speakerLabel"});

    return has_lfe_freq || has_lfe_name;
  }

  std::string GainCalculatorDirectSpeakersImpl::_nominalSpeakerLabel(
      const std::string& label) {
    std::string ret = label;
    std::smatch idMatch;
    if (std::regex_search(label, idMatch, SPEAKER_URN_REGEX)) {
      ret = idMatch[1].str();
    }
    if (_substitutions.count(label)) {
      ret = _substitutions.at(label);
    }
    return ret;
  }

  std::vector<std::pair<int, double>>
  GainCalculatorDirectSpeakersImpl::_findCandidates(
      const PolarSpeakerPosition& pos, bool isLfe, double tol) {
    Eigen::RowVector3d cartPosition = toCartesianVector3d(pos);
    std::vector<std::pair<int, double>> candidates;
    double azMin = boost::get_optional_value_or(pos.azimuthMin, pos.azimuth);
    double azMax = boost::get_optional_value_or(pos.azimuthMax, pos.azimuth);
    double elMin =
        boost::get_optional_value_or(pos.elevationMin, pos.elevation);
    double elMax =
        boost::get_optional_value_or(pos.elevationMax, pos.elevation);
    double distMin =
        boost::get_optional_value_or(pos.distanceMin, pos.distance);
    double distMax =
        boost::get_optional_value_or(pos.distanceMax, pos.distance);
    for (int i = 0; i < _nChannels; ++i) {
      Channel channel = _layout.channels()[i];
      if (isLfe == _isLfe[i]) {
        if ((insideAngleRange(_azimuths(i), azMin, azMax, tol) ||
             abs(_elevations(i)) >= 90.0 - tol) &&
            (_elevations(i) > elMin - tol) && (_elevations(i) < elMax + tol) &&
            (_distances(i) > distMin - tol) &&
            (_distances(i) < distMax + tol)) {
          double distance = (_positions.row(i) - cartPosition).norm();
          candidates.push_back(std::make_pair(i, distance));
        }
      }
    }
    return candidates;
  }

  std::vector<std::pair<int, double>>
  GainCalculatorDirectSpeakersImpl::_findCandidates(
      const CartesianSpeakerPosition& pos, bool isLfe, double tol) {
    Eigen::RowVector3d cartPosition = toCartesianVector3d(pos);
    std::vector<std::pair<int, double>> candidates;

    double XMin = boost::get_optional_value_or(pos.XMin, pos.X);
    double XMax = boost::get_optional_value_or(pos.XMax, pos.X);
    double YMin = boost::get_optional_value_or(pos.YMin, pos.Y);
    double YMax = boost::get_optional_value_or(pos.YMax, pos.Y);
    double ZMin = boost::get_optional_value_or(pos.ZMin, pos.Z);
    double ZMax = boost::get_optional_value_or(pos.ZMax, pos.Z);
    for (int i = 0; i < _nChannels; ++i) {
      Channel channel = _layout.channels()[i];
      if (isLfe == _isLfe[i]) {
        if (_positions.row(i)(0) + tol >= XMin &&
            _positions.row(i)(0) - tol <= XMax &&
            _positions.row(i)(1) + tol >= YMin &&
            _positions.row(i)(1) - tol <= YMax &&
            _positions.row(i)(2) + tol >= ZMin &&
            _positions.row(i)(2) - tol <= ZMax) {
          double distance = (_positions.row(i) - cartPosition).norm();
          candidates.push_back(std::make_pair(i, distance));
        }
      }
    }
    return candidates;
  }

  boost::optional<int>
  GainCalculatorDirectSpeakersImpl::_findChannelWithinBounds(
      const SpeakerPosition& position, bool isLfe, double tol) {
    std::vector<std::pair<int, double>> candidates;
    if (position.type() == typeid(PolarSpeakerPosition)) {
      PolarSpeakerPosition pos = boost::get<PolarSpeakerPosition>(position);
      candidates = _findCandidates(pos, isLfe, tol);
    } else {
      CartesianSpeakerPosition pos =
          boost::get<CartesianSpeakerPosition>(position);
      candidates = _findCandidates(pos, isLfe, tol);
    }
    if (candidates.size() == 0) {
      return boost::none;
    } else if (candidates.size() == 1) {
      return candidates[0].first;
    } else {
      std::sort(candidates.begin(), candidates.end(),
                [](const std::pair<int, double>& a,
                   const std::pair<int, double>& b) -> bool {
                  return a.second < b.second;
                });
      if (abs(candidates[0].second - candidates[1].second) > tol) {
        return candidates[0].first;
      }
    }
    return boost::none;
  }

  void GainCalculatorDirectSpeakersImpl::calculate(
      const DirectSpeakersTypeMetadata& metadata, OutputGains& direct,
      const WarningCB& warning_cb) {
    if (metadata.audioPackFormatID && metadata.speakerLabels.size() == 0)
      throw adm_error(
          "common definitions audioPackFormatID specified without any "
          "speakerLabels as specified in the common definitions file");
    direct.check_size(_nChannels);

    if (metadata.position.type() == typeid(CartesianSpeakerPosition))
      throw not_implemented("Cartesian position");

    double tol = 1e-5;
    bool isLfe = _isLfeChannel(metadata, warning_cb);
    direct.zero();

    if (metadata.audioPackFormatID) {
      auto found_pack = itu_packs.find(metadata.audioPackFormatID.get());
      if (found_pack != itu_packs.end()) {
        const std::string& itu_layout_name = found_pack->second;
        std::string label = _nominalSpeakerLabel(metadata.speakerLabels[0]);

        for (const MappingRule& rule : rules) {
          if (rule_applies(rule, itu_layout_name, label, _layout)) {
            for (auto& channel_gain : rule.gains) {
              auto idx = _layout.indexForName(channel_gain.first);
              ear_assert((bool)idx, "mapping channel not found");
              direct.write(idx.get(), channel_gain.second);
            }
            return;
          }
        }
      }
    }

    // try to find a speaker that matches a speakerLabel and type; earlier
    // speakerLabel values have higher priority
    for (std::string speakerLabel : metadata.speakerLabels) {
      std::string nominalLabel = _nominalSpeakerLabel(speakerLabel);
      auto it =
          std::find(_channelNames.begin(), _channelNames.end(), nominalLabel);
      if (it != _channelNames.end()) {
        int index = static_cast<int>(std::distance(_channelNames.begin(), it));
        if (isLfe == _isLfe[index]) {
          direct.write(index, 1.0);
          return;
        }
      }
    }
    // shift the nominal speaker position to the screen edges if specified
    SpeakerPosition shiftedPosition = _applyScreenEdgeLock(metadata.position);

    // otherwise, find the closest speaker with the correct type within the
    // given bounds
    boost::optional<int> index =
        _findChannelWithinBounds(shiftedPosition, isLfe, tol);
    if (index) {
      direct.write(boost::get(index), 1.0);
      return;
    }

    // otherwise, use the point source panner for non-LFE, and handle LFE
    // channels using downmixing rules

    if (isLfe) {
      auto it = std::find(_channelNames.begin(), _channelNames.end(), "LFE1");
      if (it != _channelNames.end()) {
        int index = static_cast<int>(std::distance(_channelNames.begin(), it));
        direct.write(index, 1.0);
      }
      return;
    } else {
      Eigen::Vector3d pos = toCartesianVector3d(shiftedPosition);
      Eigen::VectorXd gains = _pointSourcePanner->handle(pos).get();
      mask_write(direct, !_isLfe, gains);
      return;
    }
  }

}  // namespace ear
